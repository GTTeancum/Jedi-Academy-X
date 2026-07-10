// tr_map.c

// leave this as first line for PCH reasons...
//
#include "../server/exe_headers.h"

#include "tr_local.h"

#include "../qcommon/cm_local.h"
#ifdef STEFX_ELITE_FORCE_SP
#include "../qcommon/ef_bsp_xbox_shared.h"
#endif

#ifdef _XBOX
#include "../win32/xb_log.h"
#endif

/*

Loads and prepares a map file for scene rendering.

A single entry point:

void RE_LoadWorldMap( const char *name );

*/

world_t		s_worldData;
byte		*fileBase;
int			c_subdivisions;
int			c_gridVerts;

static int	flareNum = 0;

void R_RMGInit(void);
//===============================================================================

// We use a special hack to prevent slight differences in channels
// from exploding into big differences, as it causes lighting problems
// later on. This is the maximum channel separation for which we
// enable the hack.
#define MAX_GREYSCALE_CHANNEL_DIFF 15

static void R_ColorShiftLightingBytes16( const byte in[4], byte out[2] ) {
	// What's the largest separation between the red, green, and blue
	// channels?
	int chanDiff = max(in[0],max(in[1],in[2])) -
		min(in[0],min(in[1],in[2]));
	if (chanDiff <= MAX_GREYSCALE_CHANNEL_DIFF)
	{
		// Ensure that all color channels compress to the same value
		byte channelAvg = (in[0] + in[1] + in[2] + 1) / 3;
		out[0] = channelAvg & 0xF0;
		out[0] |= (channelAvg & 0xF0) >> 4;
		out[1] = channelAvg & 0xF0;
		out[1] |= (in[3] & 0xF0) >> 4;

		if (channelAvg % 16 >= 8)
		{
			out[0] |= 0x10;
			out[0] |= 0x01;
			out[1] |= 0x10;
		}
		if (in[4] % 16 >= 8)
		{
			out[1] |= 0x01;
		}
		return;
	}

	// Normal case for vertex colors that are not "near" greyscale
	out[0] = in[0] & 0xF0;
	out[0] |= (in[1] & 0xF0) >> 4;
	out[1] = in[2] & 0xF0;
	out[1] |= (in[3] & 0xF0) >> 4;
	
	if(in[0] % 16 >= 8) {
		out[0] |= 0x10;
	}
	if(in[1] % 16 >= 8) {
		out[0] |= 0x1;
	}
	if(in[2] % 16 >= 8) {
		out[1] |= 0x10;
	}
	if(in[3] % 16 >= 8) {
		out[1] |= 0x1;
	}
}


static void HSVtoRGB( float h, float s, float v, float rgb[3] )
{
	int i;
	float f;
	float p, q, t;

	h *= 5;

	i = floor( h );
	f = h - i;

	p = v * ( 1 - s );
	q = v * ( 1 - s * f );
	t = v * ( 1 - s * ( 1 - f ) );

	switch ( i )
	{
	case 0:
		rgb[0] = v;
		rgb[1] = t;
		rgb[2] = p;
		break;
	case 1:
		rgb[0] = q;
		rgb[1] = v;
		rgb[2] = p;
		break;
	case 2:
		rgb[0] = p;
		rgb[1] = v;
		rgb[2] = t;
		break;
	case 3:
		rgb[0] = p;
		rgb[1] = q;
		rgb[2] = v;
		break;
	case 4:
		rgb[0] = t;
		rgb[1] = p;
		rgb[2] = v;
		break;
	case 5:
		rgb[0] = v;
		rgb[1] = p;
		rgb[2] = q;
		break;
	}
}

/*
===============
R_ColorShiftLightingBytes

===============
*/
void R_ColorShiftLightingBytes( byte in[4], byte out[4] ) {
	int		shift=0, r, g, b;

	// should NOT do it if overbrightBits is 0
	if (tr.overbrightBits)
		shift = 1 - tr.overbrightBits;

	if (!shift)
	{
		out[0] = in[0];
		out[1] = in[1];
		out[2] = in[2];
		out[3] = in[3];
		return;
	}

	// shift the data based on overbright range
	r = in[0] << shift;
	g = in[1] << shift;
	b = in[2] << shift;
	
	// normalize by color instead of saturating to white
	if ( ( r | g | b ) > 255 ) {
		int		max;

		max = r > g ? r : g;
		max = max > b ? max : b;
		r = r * 255 / max;
		g = g * 255 / max;
		b = b * 255 / max;
	}

	out[0] = r;
	out[1] = g;
	out[2] = b;
	out[3] = in[3];
}

/*
===============
R_ColorShiftLightingBytes

===============
*/
static	void R_ColorShiftLightingBytes( byte in[3]) 
{
	int		shift=0, r, g, b;

	// should NOT do it if overbrightBits is 0
	if (tr.overbrightBits)
		shift = 1 - tr.overbrightBits;

	if (!shift) {
		return;	//no need if not overbright
	}
	// shift the data based on overbright range
	r = in[0] << shift;
	g = in[1] << shift;
	b = in[2] << shift;
	
	// normalize by color instead of saturating to white
	if ( ( r | g | b ) > 255 ) {
		int		max;

		max = r > g ? r : g;
		max = max > b ? max : b;
		r = r * 255 / max;
		g = g * 255 / max;
		b = b * 255 / max;
	}

	in[0] = r;
	in[1] = g;
	in[2] = b;
}


/*
===============
R_LoadLightmaps

===============
*/
#define	LIGHTMAP_SIZE	128
void R_LoadLightmaps( void *data, int len, const char *psMapName ) {
	byte		*buf, *buf_p;
	int			i;

	if ( !len ) {
		return;
	}
	buf = (byte *)data + sizeof(int);

	// we are about to upload textures
	R_SyncRenderThread();

	// create all the lightmaps
	int size = *(int*)data;
	tr.numLightmaps = len / size;

	byte* image = (byte*)Z_Malloc(size, TAG_TEMP_WORKSPACE, qfalse, 32);

	char sMapName[MAX_QPATH];
	COM_StripExtension(psMapName,sMapName);	// will already by MAX_QPATH legal, so no length check

	for ( i = 0 ; i < tr.numLightmaps ; i++ ) {
		buf_p = buf + i * size;
		memcpy(image, buf_p, size);

		char lmapName[MAX_QPATH + 32];
		Com_sprintf(lmapName, MAX_QPATH + 32, "*%s/lightmap%d",sMapName,i);
#ifdef _XBOX
		{
			static int s_xboxLightmapStatsLogCount = 0;
			const byte *dds = buf_p;
			if (s_xboxLightmapStatsLogCount < 16 &&
				size >= 128 + LIGHTMAP_SIZE * LIGHTMAP_SIZE * 2 &&
				dds[0] == 'D' && dds[1] == 'D' && dds[2] == 'S' && dds[3] == ' ')
			{
				const unsigned short *src = (const unsigned short *)(dds + 128);
				const unsigned int rgbBits = *(const unsigned int *)(dds + 88);
				const unsigned int rMask = *(const unsigned int *)(dds + 92);
				const unsigned int gMask = *(const unsigned int *)(dds + 96);
				const unsigned int bMask = *(const unsigned int *)(dds + 100);
				int minLum = 255;
				int maxLum = 0;
				int sumLum = 0;
				int p;
				for (p = 0; p < LIGHTMAP_SIZE * LIGHTMAP_SIZE; ++p)
				{
					const unsigned short c = src[p];
					const int r = ((c >> 11) & 31) * 255 / 31;
					const int g = ((c >> 5) & 63) * 255 / 63;
					const int b = (c & 31) * 255 / 31;
					const int lum = (r * 30 + g * 59 + b * 11) / 100;
					if (lum < minLum)
						minLum = lum;
					if (lum > maxLum)
						maxLum = lum;
					sumLum += lum;
				}
				XBLF("JA: XBOX_LIGHTMAP_STATS name='%s' size=%d bits=%u masks=%08x,%08x,%08x min=%d max=%d avg=%d",
					lmapName,
					size,
					rgbBits,
					rMask,
					gMask,
					bMask,
					minLum,
					maxLum,
					sumLum / (LIGHTMAP_SIZE * LIGHTMAP_SIZE));
				++s_xboxLightmapStatsLogCount;
			}
		}
#endif
		tr.lightmaps[i] = R_CreateImage( lmapName, image, 
			LIGHTMAP_SIZE, LIGHTMAP_SIZE,
			GL_DDS_RGB16_EXT,
			qfalse, 0, GL_CLAMP);
	}

	Z_Free(image);
}

#ifdef STEFX_ELITE_FORCE_SP
/*
===============
R_LoadRawLightmaps

Elite Force/Q3 BSPs store 128x128 RGB lightmaps directly in the BSP.  The
original Xbox sidecar path stores DDS RGB565 lightmaps, so EF needs this raw
BSP upload path.
===============
*/
void R_LoadRawLightmaps( void *data, int len, const char *psMapName ) {
	byte		*buf;
	int			i, j;
	int			count;
	byte		*image;
	char		sMapName[MAX_QPATH];

	if (!len) {
		return;
	}
	if (len % (LIGHTMAP_SIZE * LIGHTMAP_SIZE * 3)) {
		Com_Error(ERR_DROP, "R_LoadRawLightmaps: funny lump size %d for %s", len, psMapName);
	}

	buf = (byte *)data;
	count = len / (LIGHTMAP_SIZE * LIGHTMAP_SIZE * 3);
	tr.numLightmaps = count;

	R_SyncRenderThread();

	image = (byte *)Z_Malloc(LIGHTMAP_SIZE * LIGHTMAP_SIZE * 4, TAG_TEMP_WORKSPACE, qfalse, 32);
	COM_StripExtension(psMapName, sMapName);

	XBLF("EF: R_LoadRawLightmaps map='%s' count=%d len=%d", psMapName, count, len);

	for (i = 0; i < count; ++i) {
		byte *buf_p = buf + i * LIGHTMAP_SIZE * LIGHTMAP_SIZE * 3;
		int minLum = 255;
		int maxLum = 0;
		int sumLum = 0;
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
		float lightmapBoost = Cvar_VariableValue("r_stefxLightmapBoost");
		if (lightmapBoost < 1.0f)
		{
			lightmapBoost = 1.0f;
		}
		if (lightmapBoost > 4.0f)
		{
			lightmapBoost = 4.0f;
		}
#endif

		for (j = 0; j < LIGHTMAP_SIZE * LIGHTMAP_SIZE; ++j) {
			byte src[4];
			byte *dst;
			int lum;
			src[0] = buf_p[j * 3 + 0];
			src[1] = buf_p[j * 3 + 1];
			src[2] = buf_p[j * 3 + 2];
			src[3] = 255;
			dst = &image[j * 4];
			R_ColorShiftLightingBytes(src, dst);
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
			if (lightmapBoost > 1.0f)
			{
				int r = (int)(dst[0] * lightmapBoost + 0.5f);
				int g = (int)(dst[1] * lightmapBoost + 0.5f);
				int b = (int)(dst[2] * lightmapBoost + 0.5f);
				dst[0] = (byte)(r > 255 ? 255 : r);
				dst[1] = (byte)(g > 255 ? 255 : g);
				dst[2] = (byte)(b > 255 ? 255 : b);
			}
#endif

			lum = (dst[0] * 30 + dst[1] * 59 + dst[2] * 11) / 100;
			if (lum < minLum)
				minLum = lum;
			if (lum > maxLum)
				maxLum = lum;
			sumLum += lum;
		}

		if (i < 16) {
			XBLF("EF: RAW_LIGHTMAP_STATS index=%d min=%d max=%d avg=%d boost=%g",
				i, minLum, maxLum, sumLum / (LIGHTMAP_SIZE * LIGHTMAP_SIZE),
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
				lightmapBoost
#else
				1.0f
#endif
				);
		}

		tr.lightmaps[i] = R_CreateImage(va("*%s/lightmap%d", sMapName, i), image,
			LIGHTMAP_SIZE, LIGHTMAP_SIZE, GL_RGBA, qfalse, qfalse, GL_CLAMP);
	}

	Z_Free(image);
}

/*
===============
R_LoadXboxOptimizedLightmaps

Xbox patch BSPs keep EF geometry untouched but strip the raw RGB lightmap lump.
The matching maps/xbox/<map>.lmpdds sidecar stores one RGB565 DDS record per
lightmap and is streamed record-by-record to keep peak map-load memory down.
===============
*/
qboolean R_LoadXboxOptimizedLightmaps( const char *psMapName ) {
	fileHandle_t	h;
	int				len;
	int				size;
	int				count;
	int				i;
	byte			*record;
	byte			*image;
	char			mapNameCopy[MAX_QPATH];
	char			baseName[MAX_QPATH];
	char			sidecarName[MAX_QPATH];
	char			sMapName[MAX_QPATH];

	if ( !psMapName || !psMapName[0] ) {
		return qfalse;
	}

	Q_strncpyz( mapNameCopy, psMapName, sizeof( mapNameCopy ) );
	Q_strncpyz( baseName, COM_SkipPath( mapNameCopy ), sizeof( baseName ) );
	COM_StripExtension( baseName, baseName );
	Com_sprintf( sidecarName, sizeof( sidecarName ), "maps/xbox/%s.lmpdds", baseName );

	XBLF("STEFX: optimized lightmaps try sidecar='%s' map='%s'", sidecarName, psMapName);
	len = FS_FOpenFileRead( sidecarName, &h, qfalse );
	if ( h == 0 || len <= 0 ) {
		XBLF("STEFX: optimized lightmaps no sidecar='%s' len=%d handle=%d", sidecarName, len, h);
		return qfalse;
	}

	if ( len < (int)sizeof( int ) ) {
		FS_FCloseFile( h );
		XBLF("STEFX: optimized lightmaps rejected '%s' len=%d header too small",
			sidecarName, len);
		return qfalse;
	}

	FS_Read( &size, sizeof( int ), h );
	if ( size < 128 + LIGHTMAP_SIZE * LIGHTMAP_SIZE * 2 ||
		 (( len - (int)sizeof( int ) ) % size) != 0 ) {
		FS_FCloseFile( h );
		XBLF("STEFX: optimized lightmaps rejected '%s' len=%d record=%d",
			sidecarName, len, size);
		return qfalse;
	}

	count = ( len - (int)sizeof( int ) ) / size;
	tr.numLightmaps = count;
	R_SyncRenderThread();

	record = (byte *)Z_Malloc( size, TAG_TEMP_WORKSPACE, qfalse, 32 );
	image = (byte *)Z_Malloc( LIGHTMAP_SIZE * LIGHTMAP_SIZE * 4, TAG_TEMP_WORKSPACE, qfalse, 32 );
	COM_StripExtension( psMapName, sMapName );

	XBLF("STEFX: optimized lightmaps load '%s' map='%s' count=%d record=%d bytes=%d",
		sidecarName, psMapName, count, size, len);

	for ( i = 0; i < count; ++i ) {
		int read;
		int j;
		const unsigned short *src565;
		int minLum = 255;
		int maxLum = 0;
		int sumLum = 0;

		read = FS_Read( record, size, h );
		if ( read != size ) {
			Z_Free( record );
			Z_Free( image );
			FS_FCloseFile( h );
			XBLF("STEFX: optimized lightmaps short read '%s' index=%d read=%d expected=%d",
				sidecarName, i, read, size);
			return qfalse;
		}

		if ( i < 8 ) {
			const byte *dds = record;
			XBLF("STEFX: optimized lightmap record index=%d magic=%c%c%c%c wh=%dx%d rgbBits=%u",
				i,
				dds[0], dds[1], dds[2], dds[3],
				*(const unsigned int *)(dds + 16),
				*(const unsigned int *)(dds + 12),
				*(const unsigned int *)(dds + 88));
		}

		src565 = (const unsigned short *)(record + 128);
		for ( j = 0; j < LIGHTMAP_SIZE * LIGHTMAP_SIZE; ++j ) {
			const unsigned short c = src565[j];
			byte *dst = &image[j * 4];
			int r = ((c >> 11) & 31) * 255 / 31;
			int g = ((c >> 5) & 63) * 255 / 63;
			int b = (c & 31) * 255 / 31;
			int lum;

			dst[0] = (byte)r;
			dst[1] = (byte)g;
			dst[2] = (byte)b;
			dst[3] = 255;

			lum = (r * 30 + g * 59 + b * 11) / 100;
			if (lum < minLum)
				minLum = lum;
			if (lum > maxLum)
				maxLum = lum;
			sumLum += lum;
		}

		if ( i < 16 ) {
			XBLF("STEFX: optimized lightmap RGBA upload index=%d min=%d max=%d avg=%d",
				i, minLum, maxLum, sumLum / (LIGHTMAP_SIZE * LIGHTMAP_SIZE));
		}

		tr.lightmaps[i] = R_CreateImage( va("*%s/lightmap%d", sMapName, i), image,
			LIGHTMAP_SIZE, LIGHTMAP_SIZE, GL_RGBA, qfalse, qfalse, GL_CLAMP );
	}

	Z_Free( record );
	Z_Free( image );
	FS_FCloseFile( h );
	return qtrue;
}

#endif


/*
=================
RE_SetWorldVisData

This is called by the clipmodel subsystem so we can share the 1.8 megs of
space in big maps...
=================
*/
void RE_SetWorldVisData( SPARC<byte> *vis ) {
	tr.externalVisData = vis;
}


/*
=================
R_LoadVisibility
=================
*/
static	void R_LoadVisibility( void ) {
	int		len;

	len = ( s_worldData.numClusters + 63 ) & ~63;
	s_worldData.novis = ( unsigned char *) Hunk_Alloc( len, qfalse );
	memset( s_worldData.novis, 0xff, len );

	s_worldData.numClusters = cmg.numClusters;
	s_worldData.clusterBytes = cmg.clusterBytes;

	// CM_Load should have given us the vis data to share, so
	// we don't need to allocate another copy
	//if ( tr.externalVisData ) {
		s_worldData.vis = tr.externalVisData;
	/*} else {
		assert(0);
	}*/
}

//===============================================================================

qhandle_t R_GetShaderByNum(int shaderNum, world_t &worldData)
{
	qhandle_t	shader;

	if ( (shaderNum < 0) || (shaderNum >= worldData.numShaders) ) 
	{
		Com_Printf( "Warning: Bad index for R_GetShaderByNum - %i", shaderNum );
		return(0);
	}
	shader = RE_RegisterShader(worldData.shaders[ shaderNum ].shader);
	return(shader);
}

/*
===============
ShaderForShaderNum
===============
*/
#ifdef _XBOX
static const char *R_EFLogImageName( const image_t *image )
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
	return image->imgName;
#else
	return "<image>";
#endif
}

static void R_EFLogShaderStage( const char *context, const shader_t *shader, int stageNum, const shaderStage_t *stage )
{
	if ( !shader || !stage )
	{
		return;
	}

	XBLF("STEFX_SHADER_STAGE ctx='%s' shader='%s' stage=%d active=%d state=0x%x rgbGen=%d alphaGen=%d lightStyle=%d tc0=%d lm0=%d vtxlm0=%d img0='%s' tex0=%d tc1=%d lm1=%d vtxlm1=%d img1='%s' tex1=%d",
		context ? context : "<null>",
		shader->name,
		stageNum,
		stage->active ? 1 : 0,
		stage->stateBits,
		stage->rgbGen,
		stage->alphaGen,
		stage->lightmapStyle,
		stage->bundle[0].tcGen,
		stage->bundle[0].isLightmap ? 1 : 0,
		stage->bundle[0].vertexLightmap ? 1 : 0,
		R_EFLogImageName( stage->bundle[0].image ),
		stage->bundle[0].image ? stage->bundle[0].image->texnum : -1,
		stage->bundle[1].tcGen,
		stage->bundle[1].isLightmap ? 1 : 0,
		stage->bundle[1].vertexLightmap ? 1 : 0,
		R_EFLogImageName( stage->bundle[1].image ),
		stage->bundle[1].image ? stage->bundle[1].image->texnum : -1);
}

static void R_EFLogShaderResolve( const char *context, int shaderNum, const dshader_t *mapShader,
	const short *lightmapNum, const byte *lightmapStyles, const shader_t *shader )
{
	int i;

	XBLF("STEFX_SHADER_RESOLVE ctx='%s' map='%s' shaderNum=%d mapName='%s' mapSurf=0x%x mapCont=0x%x resolved='%s' explicit=%d default=%d passes=%d sort=%g sky=%d cull=%d multitexEnv=%d lm=%d,%d,%d,%d styles=%u,%u,%u,%u",
		context ? context : "<null>",
		s_worldData.name,
		shaderNum,
		mapShader ? mapShader->shader : "<bad>",
		mapShader ? mapShader->surfaceFlags : 0,
		mapShader ? mapShader->contentFlags : 0,
		shader ? shader->name : "<null>",
		shader ? shader->explicitlyDefined : -1,
		shader ? shader->defaultShader : -1,
		shader ? shader->numUnfoggedPasses : -1,
		shader ? (double)shader->sort : -1.0,
		(shader && shader->sky) ? 1 : 0,
		shader ? shader->cullType : -1,
		shader ? shader->multitextureEnv : -1,
		lightmapNum ? lightmapNum[0] : -999,
		lightmapNum ? lightmapNum[1] : -999,
		lightmapNum ? lightmapNum[2] : -999,
		lightmapNum ? lightmapNum[3] : -999,
		lightmapStyles ? (unsigned int)lightmapStyles[0] : 999,
		lightmapStyles ? (unsigned int)lightmapStyles[1] : 999,
		lightmapStyles ? (unsigned int)lightmapStyles[2] : 999,
		lightmapStyles ? (unsigned int)lightmapStyles[3] : 999);

	if ( shader )
	{
		for ( i = 0; i < shader->numUnfoggedPasses && i < MAX_SHADER_STAGES; ++i )
		{
			R_EFLogShaderStage( context, shader, i, &shader->stages[i] );
		}
	}
}

static void R_EFBoundsForVerts( const mapVert_t *verts, int firstVert, int numVerts, vec3_t mins, vec3_t maxs )
{
	int i;
	int j;

	ClearBounds( mins, maxs );
	if ( !verts || numVerts <= 0 )
	{
		return;
	}

	verts += firstVert;
	for ( i = 0; i < numVerts; ++i )
	{
		vec3_t point;
		for ( j = 0; j < 3; ++j )
		{
			point[j] = (float)verts[i].xyz[j];
		}
		AddPointToBounds( point, mins, maxs );
	}
}

static void R_EFSetSurfaceDebug( msurface_t *surf, int code, int shaderNum, const mapVert_t *verts, int firstVert, int numVerts )
{
	if ( !surf )
	{
		return;
	}

	surf->xboxDebugCode = code;
	surf->xboxDebugShaderNum = shaderNum;
	R_EFBoundsForVerts( verts, firstVert, numVerts, surf->xboxDebugMins, surf->xboxDebugMaxs );
}

static void R_EFSetSurfaceDebugPoint( msurface_t *surf, int code, int shaderNum, const short point[3] )
{
	int i;

	if ( !surf )
	{
		return;
	}

	surf->xboxDebugCode = code;
	surf->xboxDebugShaderNum = shaderNum;
	for ( i = 0; i < 3; ++i )
	{
		surf->xboxDebugMins[i] = point ? (float)point[i] : 0.0f;
		surf->xboxDebugMaxs[i] = point ? (float)point[i] : 0.0f;
	}
}

static void R_EFLogSurfaceShader( const char *type, int code, int shaderNum, int fogNum,
	const unsigned int vertsPacked, const unsigned int indexesPacked,
	const short *lightmapNum, const byte *lightmapStyles, const mapVert_t *verts,
	const shader_t *shader )
{
	vec3_t mins;
	vec3_t maxs;
	int firstVert = vertsPacked >> 12;
	int numVerts = vertsPacked & 0xFFF;
	int firstIndex = indexesPacked >> 12;
	int numIndexes = indexesPacked & 0xFFF;
	const dshader_t *mapShader = NULL;

	if ( shaderNum >= 0 && shaderNum < s_worldData.numShaders )
	{
		mapShader = &s_worldData.shaders[shaderNum];
	}

	if ( !mapShader || !mapShader->shader ||
		( Q_stricmp( mapShader->shader, "textures/common/black" ) &&
		  Q_stricmp( mapShader->shader, "textures/borg/static2" ) &&
		  Q_stricmp( mapShader->shader, "textures/borg/static2_nonsolid" ) &&
		  Q_stricmp( mapShader->shader, "textures/borg/borgfield" ) &&
		  Q_stricmp( mapShader->shader, "textures/borg/borgfield_nonsolid" ) &&
		  Q_stricmp( mapShader->shader, "textures/borg/borgfield_opaque" ) &&
		  Q_stricmp( mapShader->shader, "textures/borg/bars" ) &&
		  Q_stricmp( mapShader->shader, "textures/borg/bars2" ) &&
		  Q_stricmp( mapShader->shader, "textures/borg/basic1" ) &&
		  Q_stricmp( mapShader->shader, "textures/borg/forceborder" ) &&
		  Q_stricmp( mapShader->shader, "textures/borg/forceborder2" ) &&
		  Q_stricmp( mapShader->shader, "textures/borg/forceborder3" ) &&
		  Q_stricmp( mapShader->shader, "textures/borg/energy1" ) &&
		  Q_stricmp( mapShader->shader, "textures/borg/energy1_solid" ) &&
		  Q_stricmp( mapShader->shader, "textures/borg/energy1_green" ) &&
		  Q_stricmp( mapShader->shader, "textures/borg/bigborg" ) &&
		  Q_stricmp( mapShader->shader, "textures/borg/oddlight1" ) ) )
	{
		return;
	}

	R_EFBoundsForVerts( verts, firstVert, numVerts, mins, maxs );

	XBLF("STEFX_SURFACE type='%s' map='%s' code=%d shaderNum=%d mapName='%s' resolved='%s' mapSurf=0x%x mapCont=0x%x fog=%d verts=%d firstVert=%d indexes=%d firstIndex=%d lm=%d,%d,%d,%d styles=%u,%u,%u,%u boundsMin=%g,%g,%g boundsMax=%g,%g,%g default=%d explicit=%d passes=%d sort=%g",
		type ? type : "<null>",
		s_worldData.name,
		code,
		shaderNum,
		mapShader ? mapShader->shader : "<bad>",
		shader ? shader->name : "<null>",
		mapShader ? mapShader->surfaceFlags : 0,
		mapShader ? mapShader->contentFlags : 0,
		fogNum,
		numVerts,
		firstVert,
		numIndexes,
		firstIndex,
		lightmapNum ? lightmapNum[0] : -999,
		lightmapNum ? lightmapNum[1] : -999,
		lightmapNum ? lightmapNum[2] : -999,
		lightmapNum ? lightmapNum[3] : -999,
		lightmapStyles ? (unsigned int)lightmapStyles[0] : 999,
		lightmapStyles ? (unsigned int)lightmapStyles[1] : 999,
		lightmapStyles ? (unsigned int)lightmapStyles[2] : 999,
		lightmapStyles ? (unsigned int)lightmapStyles[3] : 999,
		(double)mins[0],
		(double)mins[1],
		(double)mins[2],
		(double)maxs[0],
		(double)maxs[1],
		(double)maxs[2],
		shader ? shader->defaultShader : -1,
		shader ? shader->explicitlyDefined : -1,
		shader ? shader->numUnfoggedPasses : -1,
		shader ? (double)shader->sort : -1.0);
}
#endif

static shader_t *ShaderForShaderNum( int shaderNum, const short *lightmapNum, const byte *lightmapStyles ) {
	shader_t	*shader;
	dshader_t	*dsh;
	int			originalShaderNum = shaderNum;

	shaderNum = shaderNum;
	if ( shaderNum < 0 || shaderNum >= s_worldData.numShaders ) {
		Com_Error( ERR_DROP, "ShaderForShaderNum: bad num %i", shaderNum );
	}
	dsh = &s_worldData.shaders[ shaderNum ];

	shader = R_FindShader( dsh->shader, lightmapNum, lightmapStyles, qtrue );

#ifdef _XBOX
	R_EFLogShaderResolve( "ShaderForShaderNum", originalShaderNum, dsh, lightmapNum, lightmapStyles, shader );
	{
		static int s_xboxShaderLogBudget = 0;
		qboolean stefxIntroShader = (dsh->shader && (
			!Q_stricmp( dsh->shader, "textures/common/70yearjourney" ) ||
			!Q_stricmp( dsh->shader, "textures/common/enemyspace" ) ||
			!Q_stricmp( dsh->shader, "textures/common/sevenspace" ) ||
			!Q_stricmp( dsh->shader, "textures/common/tuvokhazard" ) ));
		if ( stefxIntroShader )
		{
			XBLF("STEFX: INTRO_SHADER shaderNum=%d name='%s' mapSurf=0x%x mapCont=0x%x shader='%s' default=%d passes=%d sort=%g lm0=%d",
				originalShaderNum,
				dsh->shader,
				dsh->surfaceFlags,
				dsh->contentFlags,
				shader ? shader->name : "<null>",
				shader ? (int)shader->defaultShader : -1,
				shader ? shader->numUnfoggedPasses : -1,
				shader ? (double)shader->sort : -1.0,
				lightmapNum ? lightmapNum[0] : -999);
		}
		if (s_xboxShaderLogBudget > 0 &&
			((dsh->surfaceFlags & SURF_SKY) || shader->sky || shader->sort == SS_PORTAL))
		{
			XBLF("JA: ShaderForShaderNum #%d name='%s' mapSurf=0x%x mapCont=0x%x shaderSky=%d sort=%g default=%d lm0=%d",
				originalShaderNum,
				dsh->shader,
				dsh->surfaceFlags,
				dsh->contentFlags,
				(int)(shader->sky != NULL),
				(double)shader->sort,
				(int)shader->defaultShader,
				lightmapNum ? lightmapNum[0] : -999);
			--s_xboxShaderLogBudget;
		}
	}
#endif

	// if the shader had errors, just use default shader
	if ( shader->defaultShader ) {
#ifdef _XBOX
		R_EFLogShaderResolve( "ShaderForShaderNum.defaultFallback", originalShaderNum, dsh, lightmapNum, lightmapStyles, tr.defaultShader );
#endif
		return tr.defaultShader;
	}

	return shader;
}

#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
static surfaceType_t s_stefxSkipSurfaceData = SF_SKIP;

static qboolean R_EFShouldSkipBorgBlackBackingSurface( const dshader_t *mapShader, int surfaceCode )
{
	if ( !mapShader || Q_stricmpn( s_worldData.name, "maps/borg", 9 ) )
	{
		return qfalse;
	}

	if ( Q_stricmp( mapShader->shader, "textures/common/black" ) )
	{
		return qfalse;
	}

	return qtrue;
}

static qboolean R_EFShouldSkipRawDrawSurface( int shaderNum, int surfaceCode )
{
	const dshader_t *mapShader;

	if ( shaderNum < 0 || shaderNum >= s_worldData.numShaders )
	{
		return qfalse;
	}

	mapShader = &s_worldData.shaders[shaderNum];
	if ( mapShader->surfaceFlags & SURF_NODRAW )
	{
		return qtrue;
	}

	if ( !Q_stricmp( mapShader->shader, "noshader" ) )
	{
		return qtrue;
	}

	if ( R_EFShouldSkipBorgBlackBackingSurface( mapShader, surfaceCode ) )
	{
		return qtrue;
	}

	return qfalse;
}

static void R_EFSkipRawDrawSurface( msurface_t *surf, int shaderNum, const char *type )
{
	static int s_skipLogBudget = 64;
	const dshader_t *mapShader = ( shaderNum >= 0 && shaderNum < s_worldData.numShaders ) ? &s_worldData.shaders[shaderNum] : NULL;

	if ( surf )
	{
		surf->shader = tr.defaultShader;
		surf->data = &s_stefxSkipSurfaceData;
	}

	if ( s_skipLogBudget > 0 )
	{
		XBLF("STEFX_WORLD_SKIP raw draw surface type='%s' shaderNum=%d mapName='%s' surf=0x%x contents=0x%x",
			type ? type : "<null>",
			shaderNum,
			mapShader ? mapShader->shader : "<bad>",
			mapShader ? mapShader->surfaceFlags : 0,
			mapShader ? mapShader->contentFlags : 0);
		--s_skipLogBudget;
	}
}
#endif

bool NeedVertexColors(shader_t *shader)
{
	int i;
	shaderStage_t *stage;

	for(i=0; i<shader->numUnfoggedPasses; i++) {
		stage = &shader->stages[i];
		switch(stage->rgbGen) {
		case CGEN_EXACT_VERTEX:
		case CGEN_VERTEX:
		case CGEN_ONE_MINUS_VERTEX:
			return true;
		}
		switch(stage->alphaGen) {
		case AGEN_VERTEX:
		case AGEN_ONE_MINUS_VERTEX:
			return true;
		}
	}

	return false;
}

int NumLightMaps(shader_t *shader)
{
	int count = 0;
	int i;

	for(i=0; i<MAXLIGHTMAPS; i++) {
		if(shader->lightmapIndex[i] >= 0) {
			count++;
		} else {
			return count;
		}
	}

	return count;
}

int SurfaceFaceSize(int numVerts, int numLightMaps, bool needVertexColors,
		int numIndexes)
{
	int sfaceSize = ( int ) &((srfSurfaceFace_t *)0)->srfPoints + 
		4 /*sizeof srfPoints*/ + 
		(numVerts * sizeof(unsigned short) *
			(VERTEX_LM + numLightMaps * 2 + 
#ifdef COMPRESS_VERTEX_COLORS
			(int)needVertexColors * 4));	
#else
			(int)needVertexColors * 8));	
#endif

	// Add in tangent size - no, tangent size is included in VERTEX_LM!

	//Indices stored in 8 bits now.
	sfaceSize += numIndexes;

	return sfaceSize;
}


void BuildDrawVertTangents( drawVert_t *verts, int *indexes, int numIndexes, int numVertexes ) 
{
	int i = 0;

	for(i = 0; i < numVertexes; i++)
	{
		verts[i].tangent[0] = 0.0f;
		verts[i].tangent[1] = 0.0f;
		verts[i].tangent[2] = 0.0f;
	}

	for(i = 0; i < numIndexes; i += 3)
	{
		vec3_t vec1, vec2, du, dv, cp;
		float st0[2], st1[2], st2[2];

		Q_CastShort2FloatScale(&st0[0], &verts[indexes[i]].dvst[0], 1.f / DRAWVERT_ST_SCALE);
		Q_CastShort2FloatScale(&st0[1], &verts[indexes[i]].dvst[1], 1.f / DRAWVERT_ST_SCALE);

		Q_CastShort2FloatScale(&st1[0], &verts[indexes[i+1]].dvst[0], 1.f / DRAWVERT_ST_SCALE);
		Q_CastShort2FloatScale(&st1[1], &verts[indexes[i+1]].dvst[1], 1.f / DRAWVERT_ST_SCALE);

		Q_CastShort2FloatScale(&st2[0], &verts[indexes[i+2]].dvst[0], 1.f / DRAWVERT_ST_SCALE);
		Q_CastShort2FloatScale(&st2[1], &verts[indexes[i+2]].dvst[1], 1.f / DRAWVERT_ST_SCALE);

		vec1[0] = verts[indexes[i+1]].xyz[0] - verts[indexes[i]].xyz[0];
		vec1[1] = st1[0] - st0[0];
		vec1[2] = st1[1] - st0[1];

		vec2[0] = verts[indexes[i+2]].xyz[0] - verts[indexes[i]].xyz[0];
		vec2[1] = st2[0] - st0[0];
		vec2[2] = st2[1] - st0[1];

		CrossProduct(vec1, vec2, cp);

		if(cp[0] == 0.0f)
			cp[0] = 0.001f;

		du[0] = -cp[1] / cp[0];
		dv[0] = -cp[2] / cp[0];

		vec1[0] = verts[indexes[i+1]].xyz[1] - verts[indexes[i]].xyz[1];

		vec2[0] = verts[indexes[i+2]].xyz[1] - verts[indexes[i]].xyz[1];
	
		CrossProduct(vec1, vec2, cp);

		if(cp[0] == 0.0f)
			cp[0] = 0.001f;

		du[1] = -cp[1] / cp[0];
		dv[1] = -cp[2] / cp[0];

		vec1[0] = verts[indexes[i+1]].xyz[2] - verts[indexes[i]].xyz[2];

		vec2[0] = verts[indexes[i+2]].xyz[2] - verts[indexes[i]].xyz[2];

		CrossProduct(vec1, vec2, cp);

		if(cp[0] == 0.0f)
			cp[0] = 0.001f;

		du[2] = -cp[1] / cp[0];
		dv[2] = -cp[2] / cp[0];

		verts[indexes[i]].tangent[0] += du[0];
		verts[indexes[i]].tangent[1] += du[1];
		verts[indexes[i]].tangent[2] += du[2];

		verts[indexes[i+1]].tangent[0] += du[0];
		verts[indexes[i+1]].tangent[1] += du[1];
		verts[indexes[i+1]].tangent[2] += du[2];

		verts[indexes[i+2]].tangent[0] += du[0];
		verts[indexes[i+2]].tangent[1] += du[1];
		verts[indexes[i+2]].tangent[2] += du[2];
	}

	for(i = 0; i < numVertexes; i++)
	{
		VectorNormalizeFast(verts[i].tangent);
	}
}


void BuildMapVertTangents( mapVert_t *verts, vec3_t *tangents, short *indexes, int numIndexes, int numVertexes ) 
{
	int i = 0;

	for(i = 0; i < numVertexes; i++)
	{
		tangents[i][0] = 0.0f;
		tangents[i][1] = 0.0f;
		tangents[i][2] = 0.0f;
	}

	for(i = 0; i < numIndexes; i += 3)
	{
		vec3_t vec1, vec2, du, dv, cp;
		
		vec1[0] = verts[indexes[i+1]].xyz[0] - verts[indexes[i]].xyz[0];
		vec1[1] = (verts[indexes[i+1]].st[0] * POINTS_ST_SCALE) - 
				   (verts[indexes[i]].st[0] * POINTS_ST_SCALE);
		vec1[2] = (verts[indexes[i+1]].st[1] * POINTS_ST_SCALE) - 
				   (verts[indexes[i]].st[1] * POINTS_ST_SCALE);

		vec2[0] = verts[indexes[i+2]].xyz[0] - verts[indexes[i]].xyz[0];
		vec2[1] = (verts[indexes[i+2]].st[0] * POINTS_ST_SCALE) - 
				   (verts[indexes[i]].st[0] * POINTS_ST_SCALE);
		vec2[2] = (verts[indexes[i+2]].st[1]* POINTS_ST_SCALE) - 
				   (verts[indexes[i]].st[1] * POINTS_ST_SCALE);

		CrossProduct(vec1, vec2, cp);

		if(cp[0] == 0.0f)
			cp[0] = 0.001f;

		du[0] = -cp[1] / cp[0];
		dv[0] = -cp[2] / cp[0];

		vec1[0] = verts[indexes[i+1]].xyz[1] - verts[indexes[i]].xyz[1];

		vec2[0] = verts[indexes[i+2]].xyz[1] - verts[indexes[i]].xyz[1];

		CrossProduct(vec1, vec2, cp);

		if(cp[0] == 0.0f)
			cp[0] = 0.001f;

		du[1] = -cp[1] / cp[0];
		dv[1] = -cp[2] / cp[0];

		vec1[0] = verts[indexes[i+1]].xyz[2] - verts[indexes[i]].xyz[2];

		vec2[0] = verts[indexes[i+2]].xyz[2] - verts[indexes[i]].xyz[2];

		CrossProduct(vec1, vec2, cp);

		if(cp[0] == 0.0f)
			cp[0] = 0.001f;

		du[2] = -cp[1] / cp[0];
		dv[2] = -cp[2] / cp[0];

		tangents[indexes[i]][0] += du[0];
		tangents[indexes[i]][1] += du[1];
		tangents[indexes[i]][2] += du[2];

		tangents[indexes[i+1]][0] += du[0];
		tangents[indexes[i+1]][1] += du[1];
		tangents[indexes[i+1]][2] += du[2];

		tangents[indexes[i+2]][0] += du[0];
		tangents[indexes[i+2]][1] += du[1];
		tangents[indexes[i+2]][2] += du[2];
	}

	for(i = 0; i < numVertexes; i++)
	{
		VectorNormalizeFast(tangents[i]);
	}
}

/*
===============
ParseFace
===============
*/
static void ParseFace( dface_t *ds, mapVert_t *verts, msurface_t *surf, short *indexes, byte *&pFaceDataBuffer) 
{
	int			i, j, k;
	srfSurfaceFace_t	*cv;
	int			numPoints, numIndexes;
	short		lightmapNum[MAXLIGHTMAPS];
	int			sfaceSize, ofsIndexes;
	vec3_t		tangents[1000];

	for(i=0;i<MAXLIGHTMAPS;i++)
	{
		lightmapNum[i] = (int)ds->lightmapNum[i] - 4;
	}

	// get fog volume
	surf->fogIndex = ds->fogNum + 1;

#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	R_EFSetSurfaceDebug( surf, ds->code, ds->shaderNum, verts, ds->verts >> 12, ds->verts & 0xFFF );
	if ( R_EFShouldSkipRawDrawSurface( ds->shaderNum, ds->code ) )
	{
		R_EFSkipRawDrawSurface( surf, ds->shaderNum, "face" );
		return;
	}
#endif

	// get shader value
	surf->shader = ShaderForShaderNum( ds->shaderNum, lightmapNum, ds->lightmapStyles );
	if ( r_singleShader->integer && !surf->shader->sky ) {
		surf->shader = tr.defaultShader;
	}
#ifdef _XBOX
	R_EFLogSurfaceShader( "face", ds->code, ds->shaderNum, surf->fogIndex,
		ds->verts, ds->indexes, lightmapNum, ds->lightmapStyles, verts, surf->shader );
#endif

#ifdef _XBOX
	{
		static int s_xboxFaceShaderLogBudget = 0;
		const dshader_t *mapShader = &s_worldData.shaders[ds->shaderNum];
		if (s_xboxFaceShaderLogBudget > 0 &&
			((mapShader->surfaceFlags & SURF_SKY) || surf->shader->sky || surf->shader->sort == SS_PORTAL))
		{
			XBLF("JA: ParseFace special shaderNum=%d name='%s' mapSurf=0x%x mapCont=0x%x shader='%s' sky=%d sort=%g fog=%d verts=%d indexes=%d lm0=%d",
				ds->shaderNum,
				mapShader->shader,
				mapShader->surfaceFlags,
				mapShader->contentFlags,
				surf->shader ? surf->shader->name : "<null>",
				(int)(surf->shader && surf->shader->sky != NULL),
				surf->shader ? (double)surf->shader->sort : -1.0,
				surf->fogIndex,
				ds->verts & 0xFFF,
				ds->indexes & 0xFFF,
				lightmapNum[0]);
			--s_xboxFaceShaderLogBudget;
		}
	}
#endif

	bool needVertexColors = NeedVertexColors(surf->shader); 
	int numLightMaps = NumLightMaps(surf->shader);
	assert(numLightMaps <= 0x7F);

	numPoints = ds->verts & 0xFFF;
	if (numPoints > MAX_FACE_POINTS) {
		VID_Printf( PRINT_DEVELOPER, "MAX_FACE_POINTS exceeded: %i\n", numPoints);
	}

	numIndexes = ds->indexes & 0xFFF;

	// create the srfSurfaceFace_t
	sfaceSize = SurfaceFaceSize(numPoints,
			numLightMaps, needVertexColors, numIndexes);
	ofsIndexes = sfaceSize - numIndexes;

	cv = (srfSurfaceFace_t *) pFaceDataBuffer;//Hunk_Alloc( sfaceSize );
	pFaceDataBuffer += sfaceSize;	// :-)

	cv->surfaceType = SF_FACE;
	cv->numPoints = numPoints;
	cv->numIndices = numIndexes;
	cv->ofsIndices = ofsIndexes;
	cv->srfPoints = (unsigned short *)(((byte*)cv) + ( int ) &((srfSurfaceFace_t *)0)->srfPoints + 4);
	if(needVertexColors) {
		cv->flags = 1 << 7;
	} else {
		cv->flags = 0;
	}
	cv->flags |= (numLightMaps & 0x7F);

	//Make sure we don't overflow storage.
	assert(numPoints < 256);
	assert(numIndexes < 65536);
	assert(ofsIndexes < 65536);

	int nextSurfPoint = NEXT_SURFPOINT(cv->flags);
	verts += ds->verts >> 12;

	indexes += ds->indexes >> 12;

	BuildMapVertTangents(verts, tangents, indexes, numIndexes, numPoints);

	for ( i = 0 ; i < numPoints ; i++ ) {
		for ( j = 0 ; j < 3 ; j++ ) {
			*(cv->srfPoints + i * nextSurfPoint + j) = verts[i].xyz[j];
		}
		
		for ( j = 0; j < 3 ; j++ ) {
			assert(tangents[i][j] >= -1 && tangents[i][j] <= 1);
			*(cv->srfPoints + i * nextSurfPoint + 3 + j) = (short)(tangents[i][j] * 32767.0f);
		}
		for ( j = 0 ; j < 2 ; j++ ) {
			*(cv->srfPoints + i * nextSurfPoint + 6 + j) = 
				(short)(verts[i].st[j] * POINTS_ST_SCALE);

			for(k=0;k<numLightMaps;k++)
			{
				*(cv->srfPoints + i * nextSurfPoint + VERTEX_LM+j+(k*2)) = 
					verts[i].lightmap[k][j];
			}
		}
		if(needVertexColors) {
			for(k=0;k<MAXLIGHTMAPS;k++)
			{
#ifdef COMPRESS_VERTEX_COLORS
				R_ColorShiftLightingBytes16(
					verts[i].color[k],
					(byte*)(cv->srfPoints + i * nextSurfPoint + 
					VERTEX_COLOR(cv->flags) + k));
#else
				R_ColorShiftLightingBytes(
					verts[i].color[k],
					(byte*)(cv->srfPoints + i * nextSurfPoint + 
					VERTEX_COLOR(cv->flags) + 2*k));
#endif
			}
		}
	}

//	indexes += ds->indexes >> 12;
	unsigned char *indexStorage = ((unsigned char*)cv) + cv->ofsIndices;
	for ( i = 0 ; i < numIndexes ; i++ ) {
		indexStorage[i] = indexes[ i ];
	}

	// take the plane information from the lightmap vector
	for ( i = 0 ; i < 3 ; i++ ) {
		cv->plane.normal[i] = (float)ds->lightmapVecs[i] / 32767.f;
	}
	vec3_t fVec;
	fVec[0] = (float)((short)cv->srfPoints[0]);
	fVec[1] = (float)((short)cv->srfPoints[1]);
	fVec[2] = (float)((short)cv->srfPoints[2]);
	cv->plane.dist = DotProduct( fVec, cv->plane.normal );
	SetPlaneSignbits( &cv->plane );
	cv->plane.type = PlaneTypeForNormal( cv->plane.normal );

	surf->data = (surfaceType_t *)cv;
}


/*
===============
ParseMesh
===============
*/
static void ParseMesh ( dpatch_t *ds, mapVert_t *verts, msurface_t *surf,
					   drawVert_t* points, drawVert_t* ctrl, float* errorTable ) {
	srfGridMesh_t	*grid;
	int				i, j, k;
	int				width, height, numPoints;
	short			lightmapNum[MAXLIGHTMAPS];
	vec3_t			bounds[2];
	vec3_t			tmpVec;

	for(i=0;i<MAXLIGHTMAPS;i++)
	{
		lightmapNum[i] = (int)ds->lightmapNum[i] - 4;
	}

	// get fog volume
	surf->fogIndex = ds->fogNum + 1;

#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	R_EFSetSurfaceDebug( surf, ds->code, ds->shaderNum, verts, ds->verts >> 12, ds->verts & 0xFFF );
	if ( R_EFShouldSkipRawDrawSurface( ds->shaderNum, ds->code ) )
	{
		R_EFSkipRawDrawSurface( surf, ds->shaderNum, "patch" );
		return;
	}
#endif

	// get shader value
	surf->shader = ShaderForShaderNum( ds->shaderNum, lightmapNum, ds->lightmapStyles );
	if ( r_singleShader->integer && !surf->shader->sky ) {
		surf->shader = tr.defaultShader;
	}
#ifdef _XBOX
	R_EFLogSurfaceShader( "patch", ds->code, ds->shaderNum, surf->fogIndex,
		ds->verts, 0, lightmapNum, ds->lightmapStyles, verts, surf->shader );
#endif

	width = ds->patchWidth;
	height = ds->patchHeight;

	verts += ds->verts >> 12;
	numPoints = width * height;
	for ( i = 0 ; i < numPoints ; i++ ) {
		for ( j = 0 ; j < 3 ; j++ ) {
			points[i].xyz[j] = (float)verts[i].xyz[j];
			points[i].normal[j] = (float)verts[i].normal[j] / 32767.f;
		}
		for ( j = 0 ; j < 2 ; j++ ) {
			// Sanity check that alternate fixed point representation
			// is good enough
			assert( verts[i].st[j] * GRID_DRAWVERT_ST_SCALE < 32767 &&
					verts[i].st[j] * GRID_DRAWVERT_ST_SCALE >= -32768 );
			points[i].dvst[j] = verts[i].st[j] * GRID_DRAWVERT_ST_SCALE;
			for(k=0;k<MAXLIGHTMAPS;k++)
			{
				points[i].dvlightmap[k][j] = 
					((float)verts[i].lightmap[k][j] / POINTS_LIGHT_SCALE) *
					DRAWVERT_LIGHTMAP_SCALE;
			}
		}
		for(k=0;k<MAXLIGHTMAPS;k++)
		{
#ifdef COMPRESS_VERTEX_COLORS
			R_ColorShiftLightingBytes16(verts[i].color[k], 
				points[i].dvcolor[k]);
#else
			R_ColorShiftLightingBytes(verts[i].color[k],
				points[i].dvcolor[k]);
#endif
		}
	}

	// pre-tesseleate
	grid = R_SubdividePatchToGrid( width, height, points, ctrl, errorTable );
	surf->data = (surfaceType_t *)grid;

	// copy the level of detail origin, which is the center
	// of the group of all curves that must subdivide the same
	// to avoid cracking
	for ( i = 0 ; i < 3 ; i++ ) {
		bounds[0][i] = ds->lightmapVecs[0][i];
		bounds[1][i] = ds->lightmapVecs[1][i];
	}
	VectorAdd( bounds[0], bounds[1], bounds[1] );
	VectorScale( bounds[1], 0.5f, grid->lodOrigin );
	VectorSubtract( bounds[0], grid->lodOrigin, tmpVec );
	grid->lodRadius = VectorLength( tmpVec );
}

/*
===============
ParseTriSurf
===============
*/
static void ParseTriSurf( dtrisurf_t *ds, mapVert_t *verts, msurface_t *surf, short *indexes ) {
	srfTriangles_t	*tri;
	int				i, j, k;
	int				numVerts, numIndexes;

	// get fog volume
	surf->fogIndex = ds->fogNum + 1;

#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	R_EFSetSurfaceDebug( surf, ds->code, ds->shaderNum, verts, ds->verts >> 12, ds->verts & 0xFFF );
	if ( R_EFShouldSkipRawDrawSurface( ds->shaderNum, ds->code ) )
	{
		R_EFSkipRawDrawSurface( surf, ds->shaderNum, "trisurf" );
		return;
	}
#endif

	// get shader
	surf->shader = ShaderForShaderNum( ds->shaderNum, lightmapsVertex, ds->lightmapStyles );
	if ( r_singleShader->integer && !surf->shader->sky ) {
		surf->shader = tr.defaultShader;
	}
#ifdef _XBOX
	R_EFLogSurfaceShader( "trisurf", ds->code, ds->shaderNum, surf->fogIndex,
		ds->verts, ds->indexes, lightmapsVertex, ds->lightmapStyles, verts, surf->shader );
#endif

	numVerts = ds->verts & 0xFFF;
	numIndexes = ds->indexes & 0xFFF;

	tri = (srfTriangles_t *) Hunk_Alloc( sizeof( *tri ) + numVerts * sizeof( tri->verts[0] ) 
		+ numIndexes * sizeof( tri->indexes[0] ), qtrue );
	tri->surfaceType = SF_TRIANGLES;
	tri->numVerts = numVerts;
	tri->numIndexes = numIndexes;
	tri->verts = (drawVert_t *)(tri + 1);
	tri->indexes = (int *)(tri->verts + tri->numVerts );

	surf->data = (surfaceType_t *)tri;

	// copy vertexes
	verts += ds->verts >> 12;
	ClearBounds( tri->bounds[0], tri->bounds[1] );
	for ( i = 0 ; i < numVerts ; i++ ) {
		for ( j = 0 ; j < 3 ; j++ ) {
			tri->verts[i].xyz[j] = verts[i].xyz[j];
			tri->verts[i].normal[j] = verts[i].normal[j];
		}
		AddPointToBounds( tri->verts[i].xyz, tri->bounds[0], tri->bounds[1] );
		for ( j = 0 ; j < 2 ; j++ ) {
			// Sanity check that alternate fixed point representation
			// is good enough
			// MATT! - double check this!
			assert( verts[i].st[j] * DRAWVERT_ST_SCALE <= 32767 &&
					verts[i].st[j] * DRAWVERT_ST_SCALE >= -32768 );
			tri->verts[i].dvst[j] = verts[i].st[j] * DRAWVERT_ST_SCALE;
			for(k=0;k<MAXLIGHTMAPS;k++)
			{
				tri->verts[i].dvlightmap[k][j] = 
					((float)verts[i].lightmap[k][j] / POINTS_LIGHT_SCALE) *
					DRAWVERT_LIGHTMAP_SCALE;
			}
		}
		for(k=0;k<MAXLIGHTMAPS;k++)
		{
#ifdef COMPRESS_VERTEX_COLORS
			R_ColorShiftLightingBytes16(verts[i].color[k], 
				tri->verts[i].dvcolor[k]);
#else
			R_ColorShiftLightingBytes(verts[i].color[k],
				tri->verts[i].dvcolor[k]);
#endif
		}
	}

	// copy indexes
	indexes += ds->indexes >> 12;
	for ( i = 0 ; i < numIndexes ; i++ ) {
		tri->indexes[i] = indexes[i];
		if ( tri->indexes[i] < 0 || tri->indexes[i] >= numVerts ) {
			Com_Error( ERR_DROP, "Bad index in triangle surface" );
		}
	}

	// Build the tangent vectors
	BuildDrawVertTangents(tri->verts, tri->indexes, numIndexes, numVerts);
}


/*
===============
ParseFlare
===============
*/
static void ParseFlare( dflare_t *df, msurface_t *surf )
{
	srfFlare_t		*flare;
	int i;

	surf->fogIndex = df->fogNum + 1;

	// get shader
	surf->shader = ShaderForShaderNum( df->shaderNum, lightmapsVertex, stylesDefault );
#ifdef _XBOX
	R_EFSetSurfaceDebugPoint( surf, df->code, df->shaderNum, df->origin );
	XBLF("STEFX_SURFACE type='flare' map='%s' code=%d shaderNum=%d mapName='%s' resolved='%s' fog=%d origin=%d,%d,%d normal=%d,%d,%d color=%u,%u,%u default=%d explicit=%d passes=%d sort=%g",
		s_worldData.name,
		df->code,
		df->shaderNum,
		(df->shaderNum >= 0 && df->shaderNum < s_worldData.numShaders) ? s_worldData.shaders[df->shaderNum].shader : "<bad>",
		surf->shader ? surf->shader->name : "<null>",
		surf->fogIndex,
		df->origin[0], df->origin[1], df->origin[2],
		df->normal[0], df->normal[1], df->normal[2],
		(unsigned int)df->color[0], (unsigned int)df->color[1], (unsigned int)df->color[2],
		surf->shader ? surf->shader->defaultShader : -1,
		surf->shader ? surf->shader->explicitlyDefined : -1,
		surf->shader ? surf->shader->numUnfoggedPasses : -1,
		surf->shader ? (double)surf->shader->sort : -1.0);
#endif

	flare = (srfFlare_t *) Hunk_Alloc( sizeof( *flare ), qtrue );
	flare->surfaceType = SF_FLARE;

	for ( i = 0 ; i < 3 ; i++ ) {
		flare->origin[i] = df->origin[i];
		flare->color[i] = df->color[i];
		flare->normal[i] = df->normal[i];
	}

	assert(flareNum <= 255);
	flare->number = flareNum++;
	flare->visible = -1;

	surf->data = (surfaceType_t *)flare;
}


void R_LoadFlares( void *surfaces, int surfacelen ) {
	int count, i;
	dflare_t	*in = NULL;
	msurface_t  *out;

	count = surfacelen / sizeof(*in);

	flareNum = 0;

	for ( i = 0 ; i < count ; i++ ) {
		in = (dflare_t *)surfaces + i;
		out = s_worldData.surfaces + in->code;
		ParseFlare( in, out );
	}
}


/*
===============
R_LoadSurfaces
===============
*/
void R_LoadSurfaces( int count ) {
#ifdef _XBOX
	int i;
#endif
	s_worldData.surfaces = (struct msurface_s *) 
		Hunk_Alloc ( count * sizeof(msurface_s), qtrue );
	s_worldData.numsurfaces = count;
#ifdef _XBOX
	for ( i = 0; i < count; ++i )
	{
		s_worldData.surfaces[i].xboxDebugCode = -1;
		s_worldData.surfaces[i].xboxDebugShaderNum = -1;
		ClearBounds( s_worldData.surfaces[i].xboxDebugMins, s_worldData.surfaces[i].xboxDebugMaxs );
	}
#endif
}


/*
===============
R_LoadPatches
===============
*/
void R_LoadPatches( void *verts, int vertlen, 
					void *surfaces, int surfacelen ) {
	dpatch_t	*in = NULL;
	msurface_t	*out;
	mapVert_t	*dv;
	int			count;
	int			i;

	if (surfacelen == 0) {
		return;
	}
	
	count = surfacelen / sizeof(*in);
#ifdef _XBOX
	XBLF("JA: R_LoadPatches begin patches=%d surfaceLen=%d vertLen=%d",
		count, surfacelen, vertlen);
#endif

	dv = (mapVert_t *)(verts);
	if (vertlen % sizeof(*dv))
		Com_Error (ERR_DROP, "LoadMap: funny lump size in %s",s_worldData.name);

	drawVert_t* points = (drawVert_t*)Z_Malloc(
		MAX_PATCH_SIZE*MAX_PATCH_SIZE*sizeof(drawVert_t), 
		TAG_TEMP_WORKSPACE, qfalse);
	
	drawVert_t*	ctrl = (drawVert_t*)Z_Malloc(
		MAX_GRID_SIZE*MAX_GRID_SIZE*sizeof(drawVert_t), 
		TAG_TEMP_WORKSPACE, qfalse);
	
	float* errorTable = (float*)Z_Malloc(
		2*MAX_GRID_SIZE*sizeof(float), 
		TAG_TEMP_WORKSPACE, qfalse);

	for ( i = 0 ; i < count ; i++ ) {
		in = (dpatch_t *)surfaces + i;
		out = s_worldData.surfaces + in->code;
		ParseMesh ( in, dv, out, points, ctrl, errorTable );
	}

	Z_Free(errorTable);
	Z_Free(ctrl);
	Z_Free(points);

	VID_Printf( PRINT_ALL, "...loaded %i meshes\n", count );
}


					/*
===============
R_LoadTriSurfs
===============
*/
void R_LoadTriSurfs( void *indexdata, int indexlen, 
					void *verts, int vertlen, 
					void *surfaces, int surfacelen ) {
	dtrisurf_t	*in = NULL;
	msurface_t	*out;
	mapVert_t	*dv;
	short		*indexes;
	int			count;
	int			i;

	if (surfacelen == 0) {
		return;
	}
	
	count = surfacelen / sizeof(*in);
#ifdef _XBOX
	XBLF("JA: R_LoadTriSurfs begin trisurfs=%d surfaceLen=%d indexLen=%d vertLen=%d",
		count, surfacelen, indexlen, vertlen);
#endif

	dv = (mapVert_t *)(verts);
	if (vertlen % sizeof(*dv))
		Com_Error (ERR_DROP, "LoadMap: funny lump size in %s",s_worldData.name);

	indexes = (short *)(indexdata);
	if ( indexlen % sizeof(*indexes))
		Com_Error (ERR_DROP, "LoadMap: funny lump size in %s",s_worldData.name);

	for ( i = 0 ; i < count ; i++ ) {
		in = (dtrisurf_t *)surfaces + i;
		out = s_worldData.surfaces + in->code;
		ParseTriSurf( in, dv, out, indexes );
	}

	VID_Printf( PRINT_ALL, "...loaded %i trisurfs\n", count );
}


/*
===============
R_LoadFaces
===============
*/
void R_LoadFaces( void *indexdata, int indexlen, 
					void *verts, int vertlen, 
					void *surfaces, int surfacelen ) {
	dface_t		*in = NULL;
	msurface_t	*out;
	mapVert_t	*dv;
	short		*indexes;
	int			count;
	int			i;
#ifdef _XBOX
	int			maxFaceVerts = 0;
	int			maxFaceIndexes = 0;
	int			maxFaceFirstVert = 0;
	int			maxFaceFirstIndex = 0;
	int			maxLocalIndex = 0;
	int			faceVertsOverByte = 0;
	int			faceIndexesOverShort = 0;
	int			localIndexOverByte = 0;
	int			localIndexOutOfRange = 0;
	int			faceDataOverShort = 0;
#endif

	if (surfacelen == 0) {
		return;
	}
	
	count = surfacelen / sizeof(*in);
#ifdef _XBOX
	XBLF("JA: R_LoadFaces begin faces=%d surfaceLen=%d indexLen=%d vertLen=%d",
		count, surfacelen, indexlen, vertlen);
#endif

	dv = (mapVert_t *)(verts);
	if (vertlen % sizeof(*dv))
		Com_Error (ERR_DROP, "LoadMap: funny lump size in %s",s_worldData.name);

	indexes = (short *)(indexdata);
	if ( indexlen % sizeof(*indexes))
		Com_Error (ERR_DROP, "LoadMap: funny lump size in %s",s_worldData.name);

	// new bit, the face code on our biggest map requires over 15,000 mallocs, which was no problem on the hunk,
	//	bit hits the zone pretty bad (even the tagFree takes about 9 seconds for that many memblocks), 
	//	so special-case pre-alloc enough space for this data (the patches etc can stay as they are)...
	//
	int nTimes = count / 100;
	int nToGo = nTimes;
	int iFaceDataSizeRequired = 0;
	for ( i = 0 ; i < count ; i++) 
	{ 
		in = (dface_t *)surfaces + i;
#ifdef _XBOX
		{
			int numVerts = in->verts & 0xFFF;
			int firstVert = in->verts >> 12;
			int numIdx = in->indexes & 0xFFF;
			int firstIdx = in->indexes >> 12;
			int maxIdxThisFace = 0;
			int idx;

			if (numVerts > maxFaceVerts)
			{
				maxFaceVerts = numVerts;
			}
			if (numIdx > maxFaceIndexes)
			{
				maxFaceIndexes = numIdx;
			}
			if (firstVert > maxFaceFirstVert)
			{
				maxFaceFirstVert = firstVert;
			}
			if (firstIdx > maxFaceFirstIndex)
			{
				maxFaceFirstIndex = firstIdx;
			}
			if (numVerts > 255)
			{
				faceVertsOverByte++;
			}
			if (numIdx > 65535)
			{
				faceIndexesOverShort++;
			}

			for (idx = 0; idx < numIdx; ++idx)
			{
				int localIdx = indexes[firstIdx + idx];
				if (localIdx > maxIdxThisFace)
				{
					maxIdxThisFace = localIdx;
				}
				if (localIdx > maxLocalIndex)
				{
					maxLocalIndex = localIdx;
				}
				if (localIdx > 255)
				{
					localIndexOverByte++;
				}
				if (localIdx < 0 || localIdx >= numVerts)
				{
					localIndexOutOfRange++;
				}
			}
		}
#endif

		short lightmapNum[MAXLIGHTMAPS];
		for(int j=0; j<4; j++) {
			lightmapNum[j] = (int)in->lightmapNum[j] - 4;
		}
#ifdef _XBOX
		if ((i % 128) == 0 || i + 1 == count) {
			const char *shaderName = "<bad>";
			if (in->shaderNum >= 0 && in->shaderNum < s_worldData.numShaders) {
				shaderName = s_worldData.shaders[in->shaderNum].shader;
			}
			XBLF("JA: R_LoadFaces prepass face=%d/%d shaderNum=%d shader='%s' verts=%d indexes=%d lm0=%d",
				i + 1,
				count,
				in->shaderNum,
				shaderName,
				in->verts & 0xFFF,
				in->indexes & 0xFFF,
				lightmapNum[0]);
		}
#endif
		shader_t *shader = ShaderForShaderNum( in->shaderNum, lightmapNum, in->lightmapStyles );
		bool needVertexColors = NeedVertexColors(shader); 
		int numLightMaps = NumLightMaps(shader);
		
		int sfaceSize = SurfaceFaceSize(in->verts & 0xFFF,
			numLightMaps, needVertexColors,
			in->indexes & 0xFFF);
		
		iFaceDataSizeRequired += sfaceSize;
#ifdef _XBOX
		if (sfaceSize > 65535)
		{
			faceDataOverShort++;
		}
#endif
		assert(sfaceSize < 100 * 1024);
		if (--nToGo <= 0)
		{
			nToGo = nTimes;
		}
	}
#ifdef _XBOX
	XBLF("JA: R_LoadFaces summary faces=%d maxVerts=%d maxIndexes=%d maxFirstVert=%d maxFirstIndex=%d maxLocalIndex=%d vertsOverByte=%d localIndexOverByte=%d localIndexOutOfRange=%d faceDataBytes=%d faceDataOverShort=%d",
		count,
		maxFaceVerts,
		maxFaceIndexes,
		maxFaceFirstVert,
		maxFaceFirstIndex,
		maxLocalIndex,
		faceVertsOverByte,
		localIndexOverByte,
		localIndexOutOfRange,
		iFaceDataSizeRequired,
		faceDataOverShort);
	XBLF("JA: R_LoadFaces alloc faceDataBytes=%d", iFaceDataSizeRequired);
#endif
	in -= count;	// back it up, ready for loop-proper

	// since this ptr is to hunk data, I can pass it in and have it advanced without worrying about losing
	//	the original alloc ptr...
	//
	byte *orgFaceData;
	byte *pFaceDataBuffer	= (byte *)Hunk_Alloc( iFaceDataSizeRequired, qtrue );
	orgFaceData = pFaceDataBuffer;

	// now do regular loop...
	//
	for ( i = 0 ; i < count ; i++ ) {
		in = (dface_t *)surfaces + i;
		out = s_worldData.surfaces + in->code;
		ParseFace( in, dv, out, indexes, pFaceDataBuffer );
#ifdef _XBOX
		if (((i + 1) % 128) == 0 || i + 1 == count) {
			XBLF("JA: R_LoadFaces parsed %d/%d faceDataUsed=%d",
				i + 1,
				count,
				(int)(pFaceDataBuffer - orgFaceData));
		}
#endif
		if (--nToGo <= 0)
		{
			nToGo = nTimes;
		}
	}

	VID_Printf( PRINT_ALL, "...loaded %d faces\n", count );
}


/*
=================
R_LoadSubmodels
=================
*/
static	void R_LoadSubmodels( void *data, int len ) {
	dmodel_t	*in;
	bmodel_t	*out;
	int			i, j, count;

	in = (dmodel_t *)(data);
	if (len % sizeof(*in))
		Com_Error (ERR_DROP, "LoadMap: funny lump size in %s",s_worldData.name);
	count = len / sizeof(*in);

	s_worldData.bmodels = out = (bmodel_t *) Hunk_Alloc( count * sizeof(*out), qtrue );

	for ( i=0 ; i<count ; i++, in++, out++ ) {
		model_t *model;

		model = R_AllocModel();

		assert( model != NULL );			// this should never happen

		model->type = MOD_BRUSH;
		model->bmodel = out;
		Com_sprintf( model->name, sizeof( model->name ), "*%d", i );

		for (j=0 ; j<3 ; j++) {
			out->bounds[0][j] = in->mins[j];
			out->bounds[1][j] = in->maxs[j];
		}

		RE_InsertModelIntoHash(model->name, model);

		out->firstSurface = s_worldData.surfaces + in->firstSurface;
		out->numSurfaces = in->numSurfaces;
	}
}

//==================================================================

/*
=================
R_SetParent
=================
*/
static	void R_SetParent (mnode_t *node, mnode_t *parent)
{
	node->parent = parent;
	if (node->contents != -1)
		return;
	R_SetParent (node->children[0], node);
	R_SetParent (node->children[1], node);
}

/*
=================
R_LoadNodesAndLeafs
=================
*/
static void R_LoadNodesAndLeafs (void *nodes, int nodelen, void *leafs, int leaflen) {
	int			i, j, p;
	dnode_t		*in;
	dleaf_t		*inLeaf;
	mnode_t 	*outNode;
	mleaf_s 	*outLeaf;
	int			numNodes, numLeafs;
#ifdef _XBOX
	int			minNodeBounds[3] = { 32767, 32767, 32767 };
	int			maxNodeBounds[3] = { -32768, -32768, -32768 };
	int			minLeafBounds[3] = { 32767, 32767, 32767 };
	int			maxLeafBounds[3] = { -32768, -32768, -32768 };
	int			maxLeafArea = 0;
	int			negativeLeafAreas = 0;
	int			maxLeafCluster = -1;
	int			maxFirstMarkSurf = 0;
	int			maxLeafMarkCount = 0;
	int			maxLeafMarkEnd = 0;
	int			leafMarkEndOverflow = 0;
	int			leafMarkCountSignedOverflow = 0;
	int			nodeChildShortRisk = 0;
#endif

	in = (dnode_t *)(nodes);
	if (nodelen % sizeof(dnode_t) ||
		leaflen % sizeof(dleaf_t) ) {
		Com_Error (ERR_DROP, "LoadMap: funny lump size in %s",s_worldData.name);
	}
	numNodes = nodelen / sizeof(dnode_t);
	numLeafs = leaflen / sizeof(dleaf_t);

	outNode = (struct mnode_s *) Hunk_Alloc ( (numNodes) * sizeof(*outNode), qtrue );	
	outLeaf = (struct mleaf_s *) Hunk_Alloc ( (numLeafs) * sizeof(*outLeaf), qtrue );	

	s_worldData.nodes = outNode;
	s_worldData.leafs = outLeaf;
	s_worldData.numnodes = numNodes;
	s_worldData.numleafs = numLeafs;

	// load nodes
	for ( i=0 ; i<numNodes; i++, in++, outNode++)
	{
		for (j=0 ; j<3 ; j++)
		{
			outNode->mins[j] = in->mins[j];
			outNode->maxs[j] = in->maxs[j];
#ifdef _XBOX
			if (in->mins[j] < minNodeBounds[j])
			{
				minNodeBounds[j] = in->mins[j];
			}
			if (in->maxs[j] > maxNodeBounds[j])
			{
				maxNodeBounds[j] = in->maxs[j];
			}
#endif
		}
	
		outNode->planeNum = in->planeNum;
		outNode->contents = CONTENTS_NODE;	// differentiate from leafs

		for (j=0 ; j<2 ; j++)
		{
			p = in->children[j];
#ifdef _XBOX
			if (p == 32767 || p == -32768)
			{
				nodeChildShortRisk++;
			}
#endif
			if (p >= 0) {
				if(p < numNodes) {
					outNode->children[j] = s_worldData.nodes + p;
				} else {
					outNode->children[j] = (mnode_s*)
						(s_worldData.leafs + (p - numNodes));
				}
			} else {
				if(numNodes + (-1 - p) < numNodes) {
					outNode->children[j] = s_worldData.nodes + numNodes + (-1 - p);
				} else {
					outNode->children[j] = (mnode_s*)
						(s_worldData.leafs + (-1 - p));
				}
			}
		}
	}
	
	// load leafs
	inLeaf = (dleaf_t *)(leafs);
	for ( i=0 ; i<numLeafs ; i++, inLeaf++, outLeaf++)
	{
		for (j=0 ; j<3 ; j++)
		{
			outLeaf->mins[j] = inLeaf->mins[j];
			outLeaf->maxs[j] = inLeaf->maxs[j];
#ifdef _XBOX
			if (inLeaf->mins[j] < minLeafBounds[j])
			{
				minLeafBounds[j] = inLeaf->mins[j];
			}
			if (inLeaf->maxs[j] > maxLeafBounds[j])
			{
				maxLeafBounds[j] = inLeaf->maxs[j];
			}
#endif
		}

		outLeaf->cluster = inLeaf->cluster;
		outLeaf->area = inLeaf->area;
#ifdef _XBOX
		if (inLeaf->cluster > maxLeafCluster)
		{
			maxLeafCluster = inLeaf->cluster;
		}
		if (inLeaf->area > maxLeafArea)
		{
			maxLeafArea = inLeaf->area;
		}
		if (inLeaf->area < 0)
		{
			negativeLeafAreas++;
		}
#endif

		if ( outLeaf->cluster >= s_worldData.numClusters ) {
			s_worldData.numClusters = outLeaf->cluster + 1;
		}

		outLeaf->firstMarkSurfNum = inLeaf->firstLeafSurface;
		outLeaf->nummarksurfaces = inLeaf->numLeafSurfaces;
#ifdef _XBOX
		{
			int markEnd = (int)inLeaf->firstLeafSurface + (int)inLeaf->numLeafSurfaces;
			if (inLeaf->firstLeafSurface > maxFirstMarkSurf)
			{
				maxFirstMarkSurf = inLeaf->firstLeafSurface;
			}
			if (inLeaf->numLeafSurfaces > maxLeafMarkCount)
			{
				maxLeafMarkCount = inLeaf->numLeafSurfaces;
			}
			if (markEnd > maxLeafMarkEnd)
			{
				maxLeafMarkEnd = markEnd;
			}
			if (markEnd > 65535)
			{
				leafMarkEndOverflow++;
			}
			if (inLeaf->numLeafSurfaces > 32767)
			{
				leafMarkCountSignedOverflow++;
			}
		}
#endif
	}	
#ifdef _XBOX
	XBLF("JA: R_LoadNodesAndLeafs summary nodes=%d leafs=%d clusters=%d maxCluster=%d maxArea=%d negativeAreaLeafs=%d nodeBounds=(%d,%d,%d)-(%d,%d,%d) leafBounds=(%d,%d,%d)-(%d,%d,%d) maxFirstMark=%d maxMarkCount=%d maxMarkEnd=%d markEndOverU16=%d markCountOverS16=%d childShortRisk=%d",
		numNodes,
		numLeafs,
		s_worldData.numClusters,
		maxLeafCluster,
		maxLeafArea,
		negativeLeafAreas,
		minNodeBounds[0], minNodeBounds[1], minNodeBounds[2],
		maxNodeBounds[0], maxNodeBounds[1], maxNodeBounds[2],
		minLeafBounds[0], minLeafBounds[1], minLeafBounds[2],
		maxLeafBounds[0], maxLeafBounds[1], maxLeafBounds[2],
		maxFirstMarkSurf,
		maxLeafMarkCount,
		maxLeafMarkEnd,
		leafMarkEndOverflow,
		leafMarkCountSignedOverflow,
		nodeChildShortRisk);
#endif

	// chain decendants
	R_SetParent (s_worldData.nodes, NULL);
}

//=============================================================================

/*
=================
R_LoadShaders
=================
*/
void R_LoadShaders( void ) {	
	/*s_worldData.shaders = cm.shaders;
	s_worldData.numShaders = cm.numShaders;*/
}

/*
=================
R_LoadMarksurfaces
=================
*/
static	void R_LoadMarksurfaces (void *data, int len)
{	
	int		i, count;
	int		*in;
	msurface_t **out;
	
	in = (int *)(data);
	if (len % sizeof(*in))
		Com_Error (ERR_DROP, "LoadMap: funny lump size in %s",s_worldData.name);
	count = len / sizeof(*in);
	out = (struct msurface_s **) Hunk_Alloc ( count*sizeof(*out), qtrue );	

	s_worldData.marksurfaces = out;
	s_worldData.nummarksurfaces = count;

	for ( i=0 ; i<count ; i++)
	{
		if(in[i] > s_worldData.numsurfaces)
			assert(0);

		out[i] = s_worldData.surfaces + in[i];

		if (out[i]->shader && out[i]->shader->sort == SS_PORTAL)
		{
			s_worldData.portalPresent = qtrue;
		}
	}
}

/*
=================
R_LoadPlanes
=================
*/
static	void R_LoadPlanes( void ) {
	//New method - share with server.
	s_worldData.planes = cmg.planes;
	s_worldData.numplanes = cmg.numPlanes;
}

/*
=================
R_LoadFogs

=================
*/
static void R_LoadFogs( void *fogdata, int foglen,
					   void *brushdata, int brushlen,
					   void *sidedata, int sidelen ) {
	int			i;
	fog_t		*out;
	dfog_t		*fogs;
	dbrush_t 	*brushes, *brush;
	dbrushside_t	*sides;
	int			count, brushesCount, sidesCount;
	int			sideNum;
	int			planeNum;
	shader_t	*shader;
	float		d;
	int			firstSide=0;
	short		lightmaps[MAXLIGHTMAPS] = { LIGHTMAP_NONE } ;

	fogs = (dfog_t *)(fogdata);
	if (foglen % sizeof(*fogs)) {
		Com_Error (ERR_DROP, "LoadMap: funny lump size in %s",s_worldData.name);
	}
	count = foglen / sizeof(*fogs);

	// create fog structres for them
	// NOTE: we allocate memory for an extra one so that the LA goggles can turn on their own fog
	s_worldData.numfogs = count + 1;
	s_worldData.fogs = (fog_t *)Hunk_Alloc (( s_worldData.numfogs + 1)*sizeof(*out), qtrue );
	s_worldData.globalFog = -1;
	out = s_worldData.fogs + 1;

	if ( !count ) {
		return;
	}

	brushes = (dbrush_t *)(brushdata);
	if (brushlen % sizeof(*brushes)) {
		Com_Error (ERR_DROP, "LoadMap: funny lump size in %s",s_worldData.name);
	}
	brushesCount = brushlen / sizeof(*brushes);

	sides = (dbrushside_t *)(sidedata);
	if (sidelen % sizeof(*sides)) {
		Com_Error (ERR_DROP, "LoadMap: funny lump size in %s",s_worldData.name);
	}
	sidesCount = sidelen / sizeof(*sides);

	for ( i=0 ; i<count ; i++, fogs++) {
		out->originalBrushNumber = fogs->brushNum;
		if (out->originalBrushNumber == -1)
		{
			out->bounds[0][0] = out->bounds[0][1] = out->bounds[0][2] = MIN_WORLD_COORD;
			out->bounds[1][0] = out->bounds[1][1] = out->bounds[1][2] = MAX_WORLD_COORD;
			s_worldData.globalFog = i+1;
		}
		else
		{
			if ( (unsigned)out->originalBrushNumber >= brushesCount ) {
				Com_Error( ERR_DROP, "fog brushNumber out of range" );
			}
			brush = brushes + out->originalBrushNumber;
			
			firstSide = brush->firstSide;
			
			if ( (unsigned)firstSide > sidesCount - 6 ) {
				Com_Error( ERR_DROP, "fog brush sideNumber out of range" );
			}
			
			// brushes are always sorted with the axial sides first
			sideNum = firstSide + 0;
			planeNum = sides[ sideNum ].planeNum;
			out->bounds[0][0] = -s_worldData.planes[ planeNum ].dist;
			
			sideNum = firstSide + 1;
			planeNum = sides[ sideNum ].planeNum;
			out->bounds[1][0] = s_worldData.planes[ planeNum ].dist;
			
			sideNum = firstSide + 2;
			planeNum = sides[ sideNum ].planeNum;
			out->bounds[0][1] = -s_worldData.planes[ planeNum ].dist;
			
			sideNum = firstSide + 3;
			planeNum = sides[ sideNum ].planeNum;
			out->bounds[1][1] = s_worldData.planes[ planeNum ].dist;
			
			sideNum = firstSide + 4;
			planeNum = sides[ sideNum ].planeNum;
			out->bounds[0][2] = -s_worldData.planes[ planeNum ].dist;
			
			sideNum = firstSide + 5;
			planeNum = sides[ sideNum ].planeNum;
			out->bounds[1][2] = s_worldData.planes[ planeNum ].dist;
		}
		
		// get information from the shader for fog parameters
		shader = R_FindShader( fogs->shader, lightmaps, stylesDefault, qtrue );
		
		out->parms = *shader->fogParms;
		out->colorInt = ColorBytes4 ( shader->fogParms->color[0] * tr.identityLight, 
			shader->fogParms->color[1] * tr.identityLight, 
			shader->fogParms->color[2] * tr.identityLight, 1.0 );
		
		d = shader->fogParms->depthForOpaque < 1 ? 1 : shader->fogParms->depthForOpaque;
		out->tcScale = 1.0 / ( d * 8 );
		
		// set the gradient vector
		sideNum = fogs->visibleSide;
		
		if ( sideNum == -1 ) {
			out->hasSurface = qfalse;
		} else {
			out->hasSurface = qtrue;
			planeNum = sides[ firstSide + sideNum ].planeNum;
			VectorSubtract( vec3_origin, s_worldData.planes[ planeNum ].normal, out->surface );
			out->surface[3] = -s_worldData.planes[ planeNum ].dist;
		}
		
		out++;
	}

	// Initialise the last fog so we can use it with the LA Goggles
	// NOTE: We are might appear to be off the end of the array, but we allocated an extra memory slot above but [purposely] didn't 
	//	increment the total world numFogs to match our array size
	VectorSet(out->bounds[0], MIN_WORLD_COORD, MIN_WORLD_COORD, MIN_WORLD_COORD);
	VectorSet(out->bounds[1], MAX_WORLD_COORD, MAX_WORLD_COORD, MAX_WORLD_COORD);
	out->originalBrushNumber = -1;
	out->parms.color[0] = 0.0f;
	out->parms.color[1] = 0.0f;
	out->parms.color[2] = 0.0f;
	out->parms.color[3] = 0.0f;
	out->parms.depthForOpaque = 0.0f;
	out->colorInt = 0x00000000;
	out->tcScale = 0.0f;
	out->hasSurface = false;
}

/*
================
R_LoadLightGrid

================
*/
void R_LoadLightGrid( void *data, int len ) {
	vec3_t	maxs;
	world_t	*w;
	int		i;
	float	*wMins, *wMaxs;

	w = &s_worldData;

	w->lightGridInverseSize[0] = 1.0 / w->lightGridSize[0];
	w->lightGridInverseSize[1] = 1.0 / w->lightGridSize[1];
	w->lightGridInverseSize[2] = 1.0 / w->lightGridSize[2];

	wMins = w->bmodels[0].bounds[0];
	wMaxs = w->bmodels[0].bounds[1];

	for ( i = 0 ; i < 3 ; i++ ) {
		w->lightGridOrigin[i] = w->lightGridSize[i] * ceil( wMins[i] / w->lightGridSize[i] );
		maxs[i] = w->lightGridSize[i] * floor( wMaxs[i] / w->lightGridSize[i] );
		w->lightGridBounds[i] = (maxs[i] - w->lightGridOrigin[i])/w->lightGridSize[i] + 1;
	}

	w->lightGridData = (mgrid_t *)Hunk_Alloc( len, qfalse );
	memcpy( w->lightGridData, data, len );
}

/*
================
R_LoadLightGridArray

================
*/
void R_LoadLightGridArray( void *data, int len ) {
	world_t	*w;

	w = &s_worldData;

	w->numGridArrayElements = w->lightGridBounds[0] * w->lightGridBounds[1] * w->lightGridBounds[2];

	if ( len != w->numGridArrayElements * sizeof(*w->lightGridArray) ) {
		if (len>0)//don't warn if not even lit
			VID_Printf( PRINT_WARNING, "WARNING: light grid array mismatch\n" );
		w->lightGridData = NULL;
		return;
	}

	w->lightGridArray = (unsigned short *)Hunk_Alloc( len, qfalse );
	memcpy( w->lightGridArray, data, len );
}

/*
================
R_LoadEntities
================
*/
void R_LoadEntities( void *data, int len ) {
	const char *p, *token;
	char keyname[MAX_TOKEN_CHARS];
	char value[MAX_TOKEN_CHARS];
	world_t	*w;
	float ambient = 1;

	w = &s_worldData;
	w->lightGridSize[0] = 64;
	w->lightGridSize[1] = 64;
	w->lightGridSize[2] = 128;

	VectorSet(tr.sunAmbient, 1, 1, 1);
	tr.distanceCull = 12000;//DEFAULT_DISTANCE_CULL;

	p = (char *)(data);

	token = COM_ParseExt( &p, qtrue );
	if (!*token || *token != '{') {
		return;
	}

	// only parse the world spawn
	while ( 1 ) {	
		// parse key
		token = COM_ParseExt( &p, qtrue );

		if ( !*token || *token == '}' ) {
			break;
		}
		Q_strncpyz(keyname, token, sizeof(keyname));

		// parse value
		token = COM_ParseExt( &p, qtrue );

		if ( !*token || *token == '}' ) {
			break;
		}
		Q_strncpyz(value, token, sizeof(value));

		if (!Q_stricmp(keyname, "distanceCull")) {
			sscanf(value, "%f", &tr.distanceCull );
			continue;
		}
		//check for linear fog -rww
		if (!Q_stricmp(keyname, "linFogStart")) {
			sscanf(value, "%f", &tr.rangedFog );
			tr.rangedFog = -tr.rangedFog;
			continue;
		}
		// check for a different grid size
		if (!Q_stricmp(keyname, "gridsize")) {
			sscanf(value, "%f %f %f", &w->lightGridSize[0], &w->lightGridSize[1], &w->lightGridSize[2] );
			continue;
		}
	// find the optional world ambient for arioche
		if (!Q_stricmp(keyname, "_color")) {
			sscanf(value, "%f %f %f", &tr.sunAmbient[0], &tr.sunAmbient[1], &tr.sunAmbient[2] );
			continue;
		}
		if (!Q_stricmp(keyname, "ambient")) {
			sscanf(value, "%f", &ambient);
			continue;
		}
	}
	//both default to 1 so no harm if not present.
	VectorScale( tr.sunAmbient, ambient, tr.sunAmbient);
}

#ifdef STEFX_ELITE_FORCE_SP
void R_EFBeginRawWorldMapLoad(const char *name)
{
	tr.worldMapLoaded = qfalse;
	tr.world = NULL;
	memset(&s_worldData, 0, sizeof(s_worldData));

	if (name && name[0])
	{
		Q_strncpyz(s_worldData.name, name, sizeof(s_worldData.name));
		Q_strncpyz(s_worldData.baseName, COM_SkipPath(s_worldData.name), sizeof(s_worldData.baseName));
		COM_StripExtension(s_worldData.baseName, s_worldData.baseName);
	}
}

qboolean R_EFLoadRawWorldDataFromBSP(const char *name, const efbspFile_t *efbsp)
{
	int shaderCount;
	void *brushes;
	void *brushsides;
	void *nodes;
	void *leafs;
	void *models;
	void *lightgrid;
	void *lightarray;
	int brushesLen, brushsidesLen, nodesLen, leafsLen, modelsLen, lightgridLen, lightarrayLen;

	if (!name || !name[0] || !efbsp || !efbsp->data)
	{
		return qfalse;
	}

	EFBSP_Validate(efbsp, name);
	shaderCount = EFBSP_ShaderCount(efbsp);

	if (!s_worldData.name[0])
	{
		Q_strncpyz(s_worldData.name, name, sizeof(s_worldData.name));
		Q_strncpyz(s_worldData.baseName, COM_SkipPath(s_worldData.name), sizeof(s_worldData.baseName));
		COM_StripExtension(s_worldData.baseName, s_worldData.baseName);
	}

	XBLF("EF: R_EFLoadRawWorldData map='%s' bytes=%d shaders=%d surfaces=%d renderSurfaces=%d expectedLightgrid=%d",
		name,
		efbsp->len,
		shaderCount,
		EFBSP_SurfaceCount(efbsp),
		s_worldData.numsurfaces,
		EFBSP_ExpectedLightGridElements(efbsp));

	R_LoadPlanes();

	brushes = EFBSP_ConvertBrushes(efbsp, shaderCount, &brushesLen);
	brushsides = EFBSP_ConvertBrushSides(efbsp, shaderCount, &brushsidesLen);
	R_LoadFogs(EFBSP_LumpData(efbsp, EF_LUMP_FOGS), EFBSP_LumpLen(efbsp, EF_LUMP_FOGS),
		brushes, brushesLen, brushsides, brushsidesLen);
	EFBSP_FreeTemp(brushsides);
	EFBSP_FreeTemp(brushes);

	R_LoadMarksurfaces(EFBSP_LumpData(efbsp, EF_LUMP_LEAFSURFACES), EFBSP_LumpLen(efbsp, EF_LUMP_LEAFSURFACES));

	nodes = EFBSP_ConvertNodes(efbsp, &nodesLen);
	leafs = EFBSP_ConvertLeafs(efbsp, &leafsLen);
	R_LoadNodesAndLeafs(nodes, nodesLen, leafs, leafsLen);
	EFBSP_FreeTemp(leafs);
	EFBSP_FreeTemp(nodes);

	models = EFBSP_ConvertModels(efbsp, &modelsLen);
	R_LoadSubmodels(models, modelsLen);
	EFBSP_FreeTemp(models);

	R_LoadVisibility();
	R_LoadEntities(EFBSP_LumpData(efbsp, EF_LUMP_ENTITIES), EFBSP_LumpLen(efbsp, EF_LUMP_ENTITIES));

	lightgrid = EFBSP_ConvertLightGrid(efbsp, &lightgridLen);
	R_LoadLightGrid(lightgrid, lightgridLen);
	EFBSP_FreeTemp(lightgrid);

	lightarray = EFBSP_ConvertLightArray(efbsp, &lightarrayLen);
	R_LoadLightGridArray(lightarray, lightarrayLen);
	EFBSP_FreeTemp(lightarray);

	tr.world = &s_worldData;
	tr.worldMapLoaded = qtrue;
	XBLF("EF: R_EFLoadRawWorldData complete map='%s' nodes=%d leafs=%d marks=%d surfaces=%d models=%d lightgridLen=%d lightarrayLen=%d",
		s_worldData.name,
		s_worldData.numnodes,
		s_worldData.numleafs,
		s_worldData.nummarksurfaces,
		s_worldData.numsurfaces,
		cmg.numSubModels,
		lightgridLen,
		lightarrayLen);

	R_LoadLevelLightParms();
	R_GetLightParmsForLevel();
	return qtrue;
}
#endif


/*
=================
RE_LoadWorldMap

Called directly from cgame
=================
*/
void RE_LoadWorldMap_Actual( const char *name, world_t &worldData, int index ) {
	char		stripName[MAX_QPATH];
	Lump outputLumps[3];

#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	XBLF("EF: RE_LoadWorldMap request '%s' worldLoaded=%d current='%s' world=%08x",
		name ? name : "(null)",
		tr.worldMapLoaded,
		(tr.world && tr.world->name[0]) ? tr.world->name : "(none)",
		(unsigned int)tr.world);
#endif

	// This is no longer correct. The new code supports sub-models, apparently BSPs in
	// several chunks. If any map tries to use them, the following COM_Error will go
	// off. We haven't hit it yet, but if (when) we do, check out tr_bsp.cpp for changes.
	if ( tr.worldMapLoaded ) {
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
		const char *currentName = (tr.world && tr.world->name[0]) ? tr.world->name : "";
		if (name && currentName[0] && !Q_stricmp(currentName, name)) {
			XBLF("EF: RE_LoadWorldMap duplicate same-map request '%s'; keeping existing world", name);
			return;
		}
		XBLF("EF: RE_LoadWorldMap duplicate mismatch requested='%s' current='%s'",
			name ? name : "(null)",
			currentName[0] ? currentName : "(none)");
#endif
		Com_Error( ERR_DROP, "ERROR: attempted to redundantly load world map\n" );
	}

	// set default sun direction to be used if it isn't
	// overridden by a shader
	skyboxportal = 0;

	tr.sunDirection[0] = 0.45f;
	tr.sunDirection[1] = 0.3f;
	tr.sunDirection[2] = 0.9f;

	VectorNormalize( tr.sunDirection );

	Cvar_SetValue( "r_sundir_x", tr.sunDirection[0] );
	Cvar_SetValue( "r_sundir_y", tr.sunDirection[1] );
	Cvar_SetValue( "r_sundir_z", tr.sunDirection[2] );

	tr.worldMapLoaded = qtrue;

	// clear tr.world so if the level fails to load, the next
	// try will not look at the partially loaded version
	tr.world = NULL;

	//Preserve data which was already set in cm_load
	msurface_t *surfacePtr = s_worldData.surfaces;
	int numSurfaces = s_worldData.numsurfaces;
	memset( &s_worldData, 0, sizeof( s_worldData ) );
	s_worldData.surfaces = surfacePtr;
	s_worldData.numsurfaces = numSurfaces;
	//s_worldData.shaders = cm.shaders;
	s_worldData.numShaders = cmg.numShaders;

	Q_strncpyz( s_worldData.name, name, sizeof( s_worldData.name ) );

	Q_strncpyz( s_worldData.baseName, COM_SkipPath( s_worldData.name ), sizeof( s_worldData.name ) );
	COM_StripExtension( s_worldData.baseName, s_worldData.baseName );

	COM_StripExtension(name, stripName);
	
	c_gridVerts = 0;

#ifdef STEFX_ELITE_FORCE_SP
	{
		efbspFile_t efbsp;
		if (EFBSP_LoadFile(name, &efbsp))
		{
			R_EFLoadRawWorldDataFromBSP(name, &efbsp);
			EFBSP_FreeFile(&efbsp);
			return;
		}

		XBLF("EF: RE_LoadWorldMap raw BSP '%s' not found; emergency sidecar fallback only", name);
	}
#endif

	// load into heap
	R_LoadPlanes ();

	outputLumps[0].load(stripName, "fogs");
	outputLumps[1].load(stripName, "brushes");
	outputLumps[2].load(stripName, "brushsides");
	R_LoadFogs( outputLumps[0].data, outputLumps[0].len,
		outputLumps[1].data, outputLumps[1].len,
		outputLumps[2].data, outputLumps[2].len );
	outputLumps[2].clear();
	outputLumps[1].clear();

	outputLumps[0].load(stripName, "leafsurfaces");
	R_LoadMarksurfaces (outputLumps[0].data, outputLumps[0].len);

	outputLumps[0].load(stripName, "nodes");
	outputLumps[1].load(stripName, "leafs");
	R_LoadNodesAndLeafs (outputLumps[0].data, outputLumps[0].len,
		outputLumps[1].data, outputLumps[1].len);
	outputLumps[1].clear();
	
	outputLumps[0].load(stripName, "models");
	R_LoadSubmodels (outputLumps[0].data, outputLumps[0].len);

	R_LoadVisibility();

	outputLumps[0].load(stripName, "entities");
	R_LoadEntities( outputLumps[0].data, outputLumps[0].len );
	outputLumps[0].load(stripName, "lightgrid");
	R_LoadLightGrid( outputLumps[0].data, outputLumps[0].len );
	outputLumps[0].load(stripName, "lightarray");
	R_LoadLightGridArray( outputLumps[0].data, outputLumps[0].len );

	// only set tr.world now that we know the entire level has loaded properly
	tr.world = &s_worldData;

	// Load the light parms for this level
	R_LoadLevelLightParms();
	R_GetLightParmsForLevel();
}


// new wrapper used for convenience to tell z_malloc()-fail recovery code whether it's safe to dump the cached-bsp or not.
//
extern qboolean gbUsingCachedMapDataRightNow;
void RE_LoadWorldMap( const char *name )
{
	memset(entityVisList, -1, sizeof(entityVisList));

	gbUsingCachedMapDataRightNow = qtrue;	// !!!!!!!!!!!!

		RE_LoadWorldMap_Actual( name, s_worldData, 0 );

	gbUsingCachedMapDataRightNow = qfalse;	// !!!!!!!!!!!!
}


//A nasty looking function which loops through all images used by all surfaces
//and returns the number of matches for the given image.
#ifndef FINAL_BUILD
int R_SurfaceImageCount(const image_t *image1)
{
	int count = 0;

	for(int i=0; i<s_worldData.numsurfaces; i++) {
		for(int j=0; j<s_worldData.surfaces[i].shader->numUnfoggedPasses; j++){
			for(int k=0; k<NUM_TEXTURE_BUNDLES; k++) {
				image_t *image2 = s_worldData.surfaces[i].shader->stages[j].bundle[k].image;
				if(image2 != NULL && !Q_stricmp(image1->imgName, image2->imgName)) {
					count++;
				}
							
			}
		}
	}

	return count;
}
#endif
