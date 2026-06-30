// tr_models.c -- model loading and caching

// leave this as first line for PCH reasons...
//
#include "../server/exe_headers.h"

#include "tr_local.h"
#include "MatComp.h"
#include "../qcommon/sstring.h"
#ifdef _XBOX
#include "../win32/xb_log.h"
#endif

#define	LL(x) x=LittleLong(x)
#ifndef MD4_IDENT
#define MD4_IDENT			(('5'<<24)+('M'<<16)+('D'<<8)+'R')
#endif

void RE_LoadWorldMap_Actual( const char *name, world_t &worldData, int index ); //should only be called for sub-bsp instances

static qboolean R_LoadMD3 (model_t *mod, int lod, void *buffer, const char *name, qboolean &bAlreadyCached );
#ifdef STEFX_ELITE_FORCE_SP
static qboolean R_LoadMDR (model_t *mod, void *buffer, const char *name, qboolean &bAlreadyCached );
#endif

/*
Ghoul2 Insert Start
*/

typedef	struct modelHash_s
{
	char		name[MAX_QPATH];
	qhandle_t	handle;
	struct		modelHash_s	*next;

}modelHash_t;

#define FILE_HASH_SIZE		1024
static	modelHash_t 		*mhHashTable[FILE_HASH_SIZE];


/*
Ghoul2 Insert End
*/



// This stuff looks a bit messy, but it's kept here as black box, and nothing appears in any .H files for other 
//	modules to worry about. I may make another module for this sometime.
//
typedef pair<int,int> StringOffsetAndShaderIndexDest_t;
typedef vector <StringOffsetAndShaderIndexDest_t> ShaderRegisterData_t;
struct CachedEndianedModelBinary_s
{
	void	*pModelDiskImage;
	int		iAllocSize;		// may be useful for mem-query, but I don't actually need it
#ifdef _XBOX
	qboolean bHeapAllocated;
#endif
	ShaderRegisterData_t ShaderRegisterData;

	int		iLastLevelUsedOn;

	CachedEndianedModelBinary_s()
	{
		pModelDiskImage = 0;
		iLastLevelUsedOn    = -1;
		iAllocSize = 0;
#ifdef _XBOX
		bHeapAllocated = qfalse;
#endif
		ShaderRegisterData.clear();
	}
};
typedef struct CachedEndianedModelBinary_s CachedEndianedModelBinary_t;
typedef map <sstring_t,CachedEndianedModelBinary_t>	CachedModels_t;
													CachedModels_t *CachedModels = NULL;	// the important cache item.

#ifdef _XBOX
static void RE_RegisterModels_FreeDiskImage(CachedEndianedModelBinary_t &cachedModel)
{
	if (!cachedModel.pModelDiskImage)
	{
		return;
	}

	if (cachedModel.bHeapAllocated)
	{
		HeapFree(GetProcessHeap(), 0, cachedModel.pModelDiskImage);
	}
	else
	{
		Z_Free(cachedModel.pModelDiskImage);
	}

	cachedModel.pModelDiskImage = NULL;
	cachedModel.bHeapAllocated = qfalse;
}
#endif

void RE_RegisterModels_StoreShaderRequest(const char *psModelFileName, const char *psShaderName, const int *piShaderIndexPoke)
{
	char sModelName[MAX_QPATH];

	Q_strncpyz(sModelName,psModelFileName,sizeof(sModelName));
	Q_strlwr  (sModelName);

	CachedEndianedModelBinary_t &ModelBin = (*CachedModels)[sModelName];

	if (ModelBin.pModelDiskImage == NULL)
	{	
		assert(0);	// should never happen, means that we're being called on a model that wasn't loaded
	}
	else
	{
		const int iNameOffset =		  psShaderName		- (char *)ModelBin.pModelDiskImage;
		const int iPokeOffset = (char*) piShaderIndexPoke	- (char *)ModelBin.pModelDiskImage;

		ModelBin.ShaderRegisterData.push_back( StringOffsetAndShaderIndexDest_t( iNameOffset,iPokeOffset) );
	}
}


static const byte FakeGLAFile[] = 
{
0x32, 0x4C, 0x47, 0x41, 0x06, 0x00, 0x00, 0x00, 0x2A, 0x64, 0x65, 0x66, 0x61, 0x75, 0x6C, 0x74,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3F, 0x01, 0x00, 0x00, 0x00,
0x14, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x18, 0x01, 0x00, 0x00, 0x68, 0x00, 0x00, 0x00,
0x26, 0x01, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x4D, 0x6F, 0x64, 0x56, 0x69, 0x65, 0x77, 0x20,
0x69, 0x6E, 0x74, 0x65, 0x72, 0x6E, 0x61, 0x6C, 0x20, 0x64, 0x65, 0x66, 0x61, 0x75, 0x6C, 0x74,
0x00, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD,
0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD,
0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFD, 0xBF, 0xFE, 0x7F, 0xFE, 0x7F, 0xFE, 0x7F,
0x00, 0x80, 0x00, 0x80, 0x00, 0x80
};


// returns qtrue if loaded, and sets the supplied qbool to true if it was from cache (instead of disk)
//   (which we need to know to avoid LittleLong()ing everything again (well, the Mac needs to know anyway)...
//
qboolean RE_RegisterModels_GetDiskFile( const char *psModelFileName, void **ppvBuffer, qboolean *pqbAlreadyCached)
{
	char sModelName[MAX_QPATH];
	int len;

	Q_strncpyz(sModelName,psModelFileName,sizeof(sModelName));
	Q_strlwr  (sModelName);

	CachedEndianedModelBinary_t &ModelBin = (*CachedModels)[sModelName];

	if (ModelBin.pModelDiskImage == NULL)
	{
		// didn't have it cached, so try the disk...
		//

			// special case intercept first...
			//
			if (!strcmp(sDEFAULT_GLA_NAME ".gla" , psModelFileName))
			{
				// return fake params as though it was found on disk...
				//
				void *pvFakeGLAFile = Z_Malloc( sizeof(FakeGLAFile), TAG_FILESYS, qfalse );
				memcpy(pvFakeGLAFile, &FakeGLAFile[0],  sizeof(FakeGLAFile));
				*ppvBuffer = pvFakeGLAFile;
				*pqbAlreadyCached = qfalse;	// faking it like this should mean that it works fine on the Mac as well
				return qtrue;	
			}

		if ( ppvBuffer )
		{
			*ppvBuffer = NULL;
		}
		len = FS_ReadFile( psModelFileName, ppvBuffer );
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
		if ( len <= 0 && strcmp( sModelName, psModelFileName ) )
		{
			if ( ppvBuffer )
			{
				*ppvBuffer = NULL;
			}
			int lowerLen = FS_ReadFile( sModelName, ppvBuffer );
			if ( strstr( psModelFileName, "models/players/" ) || strstr( psModelFileName, "models\\players\\" ) )
			{
				XBLF( "STEFX: model disk lower retry original='%s' lower='%s' len=%d success=%d buffer=%p",
					psModelFileName,
					sModelName,
					lowerLen,
					(ppvBuffer && *ppvBuffer) ? 1 : 0,
					ppvBuffer ? *ppvBuffer : NULL );
			}
			if ( lowerLen > 0 || (ppvBuffer && *ppvBuffer) )
			{
				len = lowerLen;
			}
		}
#endif
		*pqbAlreadyCached = qfalse;

		const bool bSuccess = !!(*ppvBuffer);
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
		if ( strstr( psModelFileName, "models/players/borg" ) || strstr( psModelFileName, "models\\players\\borg" ) )
		{
			XBLF( "STEFX: model disk fetch '%s' cacheKey='%s' len=%d success=%d buffer=%p",
				psModelFileName,
				sModelName,
				len,
				bSuccess ? 1 : 0,
				*ppvBuffer );
		}
#endif

		return bSuccess;
	}
	else
	{
		*ppvBuffer = ModelBin.pModelDiskImage;
		*pqbAlreadyCached = qtrue;
		return qtrue;
	}
}


// if return == true, no further action needed by the caller...
//
void *RE_RegisterModels_Malloc(int iSize, void *pvDiskBufferIfJustLoaded, const char *psModelFileName, qboolean *pqbAlreadyFound, memtag_t eTag)
{
	char sModelName[MAX_QPATH];

	Q_strncpyz(sModelName,psModelFileName,sizeof(sModelName));
	Q_strlwr  (sModelName);

	CachedEndianedModelBinary_t &ModelBin = (*CachedModels)[sModelName];

	if (ModelBin.pModelDiskImage == NULL)
	{
		// ... then this entry has only just been created, ie we need to load it fully...
		//
		// new, instead of doing a Z_Malloc and assigning that we just morph the disk buffer alloc
		//	then don't thrown it away on return - cuts down on mem overhead
		//
		// ... groan, but not if doing a limb hierarchy creation (some VV stuff?), in which case it's NULL
		//			
#ifndef _XBOX
		if ( pvDiskBufferIfJustLoaded )
		{
			Z_MorphMallocTag( pvDiskBufferIfJustLoaded, eTag );
		}
		else
#endif
		{
#ifdef _XBOX
			if (eTag == TAG_MODEL_MD3 || eTag == TAG_MODEL_GLM || eTag == TAG_MODEL_GLA)
			{
				int nameLen = strlen(sModelName);
#if defined(STEFX_ELITE_FORCE_SP)
				if (pvDiskBufferIfJustLoaded &&
					nameLen >= 4 &&
					!stricmp(&sModelName[nameLen - 4], ".mdr"))
				{
					ModelBin.bHeapAllocated = qfalse;
#ifdef _XBOX
					if (strstr(sModelName, "models/players/"))
					{
						XBLF("STEFX: RE_RegisterModels_Malloc adopted MDR disk buffer model='%s' size=%d tag=%d",
							sModelName, iSize, eTag);
					}
#endif
				}
				else
#endif
				{
					pvDiskBufferIfJustLoaded = HeapAlloc(GetProcessHeap(), 0, iSize);
					if (!pvDiskBufferIfJustLoaded)
					{
#if defined(STEFX_ELITE_FORCE_SP)
						XBLF("STEFX: RE_RegisterModels_Malloc heap failed model='%s' size=%d tag=%d; falling back to zone",
							sModelName, iSize, eTag);
						ModelBin.bHeapAllocated = qfalse;
						pvDiskBufferIfJustLoaded = Z_Malloc(iSize, eTag, qfalse);
						if (!pvDiskBufferIfJustLoaded)
						{
							XBLF("STEFX: RE_RegisterModels_Malloc zone fallback failed model='%s' size=%d tag=%d; returning bad model",
								sModelName, iSize, eTag);
							ModelBin.pModelDiskImage = NULL;
							ModelBin.iAllocSize = 0;
							ModelBin.bHeapAllocated = qfalse;
							*pqbAlreadyFound = qfalse;
							return NULL;
						}
#else
						pvDiskBufferIfJustLoaded = Z_Malloc(iSize, eTag, qfalse);
						ModelBin.bHeapAllocated = qfalse;
#endif
					}
					else
					{
						ModelBin.bHeapAllocated = qtrue;
					}
				}
			}
			else
#endif
			{
			pvDiskBufferIfJustLoaded =  Z_Malloc(iSize,eTag, qfalse );
			}
		}

		ModelBin.pModelDiskImage= pvDiskBufferIfJustLoaded;
		ModelBin.iAllocSize		= iSize;
		*pqbAlreadyFound		= qfalse;
	}
	else
	{
#ifdef _XBOX
		if (eTag == TAG_MODEL_GLA && strstr(sModelName, "_humanoid"))
		{
			Com_PrintfAlways("JA: RE_RegisterModels_Malloc cache hit '%s' size=%d tag=%d\n",
				sModelName, ModelBin.iAllocSize, eTag);
		}
#endif
		// if we already had this model entry, then re-register all the shaders it wanted...
		//
		const int iEntries = ModelBin.ShaderRegisterData.size();
		for (int i=0; i<iEntries; i++)
		{
			int iShaderNameOffset	= ModelBin.ShaderRegisterData[i].first;
			int iShaderPokeOffset	= ModelBin.ShaderRegisterData[i].second;

			const char *const psShaderName	 =		   &((char*)ModelBin.pModelDiskImage)[iShaderNameOffset];
				  int  *const piShaderPokePtr= (int *) &((char*)ModelBin.pModelDiskImage)[iShaderPokeOffset];

			shader_t *sh = R_FindShader( psShaderName, lightmapsNone, stylesDefault, qtrue );
	            
			if ( sh->defaultShader ) 
			{
				*piShaderPokePtr = 0;
			} else {
				*piShaderPokePtr = sh->index;
			}
		}
		*pqbAlreadyFound = qtrue;	// tell caller not to re-Endian or re-Shader this binary		
	}

	ModelBin.iLastLevelUsedOn = RE_RegisterMedia_GetLevel();

	return ModelBin.pModelDiskImage;
}


// dump any models not being used by this level if we're running low on memory...
//
static int GetModelDataAllocSize(void)
{
	return	Z_MemSize( TAG_MODEL_MD3) +
			Z_MemSize( TAG_MODEL_GLM) +
			Z_MemSize( TAG_MODEL_GLA);
}

static int GetCachedModelDataAllocSize(int *heapBytes, int *zoneBytes, int *modelCount)
{
	int totalBytes = 0;

	if (heapBytes)
	{
		*heapBytes = 0;
	}
	if (zoneBytes)
	{
		*zoneBytes = 0;
	}
	if (modelCount)
	{
		*modelCount = 0;
	}
	if (!CachedModels)
	{
		return 0;
	}

	for (CachedModels_t::iterator itModel = CachedModels->begin(); itModel != CachedModels->end(); ++itModel)
	{
		CachedEndianedModelBinary_t &cachedModel = (*itModel).second;
		if (!cachedModel.pModelDiskImage || cachedModel.iAllocSize <= 0)
		{
			continue;
		}

		totalBytes += cachedModel.iAllocSize;
		if (modelCount)
		{
			++(*modelCount);
		}
#ifdef _XBOX
		if (cachedModel.bHeapAllocated)
		{
			if (heapBytes)
			{
				*heapBytes += cachedModel.iAllocSize;
			}
		}
		else
#endif
		{
			if (zoneBytes)
			{
				*zoneBytes += cachedModel.iAllocSize;
			}
		}
	}

	return totalBytes;
}
extern cvar_t *r_modelpoolmegs;
//
// return qtrue if at least one cached model was freed (which tells z_malloc()-fail recovery code to try again)
//
extern qboolean gbInsideRegisterModel;
qboolean RE_RegisterModels_LevelLoadEnd(qboolean bDeleteEverythingNotUsedThisLevel /* = qfalse */)
{	
	qboolean bAtLeastoneModelFreed = qfalse;

	if (gbInsideRegisterModel)
	{
		Com_DPrintf( "(Inside RE_RegisterModel (z_malloc recovery?), exiting...\n");
	}
	else
	{
		int iHeapModelBytes = 0;
		int iZoneCachedModelBytes = 0;
		int iCachedModelCount = 0;
		int iLoadedModelBytes	=	GetCachedModelDataAllocSize(&iHeapModelBytes, &iZoneCachedModelBytes, &iCachedModelCount);
		const int iMaxModelBytes=	r_modelpoolmegs->integer * 1024 * 1024;

		qboolean bEraseOccured = qfalse;
		int iFreedModelBytes = 0;
		int iFreedModelCount = 0;
#ifdef _XBOX
		const int iZoneTaggedModelBytes = GetModelDataAllocSize();
#endif
		for (CachedModels_t::iterator itModel = CachedModels->begin(); itModel != CachedModels->end() && ( bDeleteEverythingNotUsedThisLevel || iLoadedModelBytes > iMaxModelBytes ); bEraseOccured?itModel:++itModel)
		{			
			bEraseOccured = qfalse;

			CachedEndianedModelBinary_t &CachedModel = (*itModel).second;
			const int iCachedAllocSize = CachedModel.iAllocSize;

			qboolean bDeleteThis = qfalse;

			if (bDeleteEverythingNotUsedThisLevel)
			{
				bDeleteThis = (CachedModel.iLastLevelUsedOn != RE_RegisterMedia_GetLevel());
			}
			else
			{
				bDeleteThis = (CachedModel.iLastLevelUsedOn < RE_RegisterMedia_GetLevel());
			}

			// if it wasn't used on this level, dump it...
			//
			if (bDeleteThis)
			{
	#ifdef _DEBUG
//				LPCSTR psModelName = (*itModel).first.c_str();
//				VID_Printf( PRINT_DEVELOPER, "Dumping \"%s\"", psModelName);
//				VID_Printf( PRINT_DEVELOPER, ", used on lvl %d\n",CachedModel.iLastLevelUsedOn);
	#endif				

				if (CachedModel.pModelDiskImage) {
#ifdef _XBOX
					RE_RegisterModels_FreeDiskImage(CachedModel);
#else
					Z_Free(CachedModel.pModelDiskImage);	
#endif
					//CachedModel.pModelDiskImage = NULL;	// REM for reference, erase() call below negates the need for it.
					bAtLeastoneModelFreed = qtrue;
					iFreedModelBytes += iCachedAllocSize;
					++iFreedModelCount;
				}

				itModel = CachedModels->erase(itModel);
				bEraseOccured = qtrue;

				iLoadedModelBytes = GetCachedModelDataAllocSize(&iHeapModelBytes, &iZoneCachedModelBytes, &iCachedModelCount);
			}
		}
#ifdef _XBOX
		if (iFreedModelCount > 0 || iLoadedModelBytes > iMaxModelBytes)
		{
			XBLF("STEFX: model cache level-end level=%d cached=%d heap=%d zoneCache=%d zoneTagged=%d cap=%d freed=%d freedBytes=%d force=%d overBudget=%d",
				RE_RegisterMedia_GetLevel(),
				iLoadedModelBytes,
				iHeapModelBytes,
				iZoneCachedModelBytes,
				iZoneTaggedModelBytes,
				iMaxModelBytes,
				iFreedModelCount,
				iFreedModelBytes,
				bDeleteEverythingNotUsedThisLevel ? 1 : 0,
				iLoadedModelBytes > iMaxModelBytes ? 1 : 0);
		}
#endif
	}

	//VID_Printf( PRINT_DEVELOPER, "RE_RegisterModels_LevelLoadEnd(): Ok\n");	

	return bAtLeastoneModelFreed;	
}

void RE_RegisterModels_Info_f( void )
{	
	int iTotalBytes = 0;
	if(!CachedModels) {
		Com_Printf ("%d bytes total (%.2fMB)\n",iTotalBytes, (float)iTotalBytes / 1024.0f / 1024.0f);
		return;
	}

	int iModels = CachedModels->size();
	int iModel  = 0;

	for (CachedModels_t::iterator itModel = CachedModels->begin(); itModel != CachedModels->end(); ++itModel,iModel++)
	{	
		CachedEndianedModelBinary_t &CachedModel = (*itModel).second;

		VID_Printf( PRINT_ALL, "%d/%d: \"%s\" (%d bytes)",iModel,iModels,(*itModel).first.c_str(),CachedModel.iAllocSize );

		#ifdef _DEBUG
		VID_Printf( PRINT_ALL, ", lvl %d\n",CachedModel.iLastLevelUsedOn);
		#endif

		iTotalBytes += CachedModel.iAllocSize;
	}
	VID_Printf( PRINT_ALL, "%d bytes total (%.2fMB)\n",iTotalBytes, (float)iTotalBytes / 1024.0f / 1024.0f);
}


static void RE_RegisterModels_DeleteAll(void)
{
	for (CachedModels_t::iterator itModel = CachedModels->begin(); itModel != CachedModels->end(); )
	{
		CachedEndianedModelBinary_t &CachedModel = (*itModel).second;

		if (CachedModel.pModelDiskImage) {
#ifdef _XBOX
			RE_RegisterModels_FreeDiskImage(CachedModel);
#else
			Z_Free(CachedModel.pModelDiskImage);					
#endif
		}

		itModel = CachedModels->erase(itModel);			
	}

	extern void RE_AnimationCFGs_DeleteAll(void);
	RE_AnimationCFGs_DeleteAll();
}


static int giRegisterMedia_CurrentLevel=0;
static qboolean gbAllowScreenDissolve = qtrue;
#ifdef _XBOX
extern bool g_xboxDirectMapBootQueued;
extern bool Sys_IsDirectMapBoot(void);
#endif
//
// param "bAllowScreenDissolve" is just a convenient way of getting hold of a bool which can be checked by the code that
//	issues the InitDissolve command later in RE_RegisterMedia_LevelLoadEnd()
//
void RE_RegisterMedia_LevelLoadBegin(const char *psMapName, ForceReload_e eForceReload, qboolean bAllowScreenDissolve)
{
	gbAllowScreenDissolve = bAllowScreenDissolve;

	tr.numBSPModels = 0;

	// for development purposes we may want to ditch certain media just before loading a map...
	//
	switch (eForceReload)
	{
		case eForceReload_BSP:

			CM_DeleteCachedMap(qtrue);
			R_Images_DeleteLightMaps();
			break;

		case eForceReload_MODELS:

			RE_RegisterModels_DeleteAll();
			break;

		case eForceReload_ALL:

			// BSP...
			//
			CM_DeleteCachedMap(qtrue);
			R_Images_DeleteLightMaps();
			//
			// models...
			//
			RE_RegisterModels_DeleteAll();
			break;
	}

	// at some stage I'll probably want to put some special logic here, like not incrementing the level number
	//	when going into a map like "brig" or something, so returning to the previous level doesn't require an 
	//	asset reload etc, but for now...
	//
	// only bump level number if we're not on the same level. 
	//	Note that this will hide uncached models, which is perhaps a bad thing?...
	//
	static char sPrevMapName[MAX_QPATH]={0};
	if (Q_stricmp( psMapName,sPrevMapName ))
	{
		Q_strncpyz( sPrevMapName, psMapName, sizeof(sPrevMapName) );
		giRegisterMedia_CurrentLevel++;
	}
}

int RE_RegisterMedia_GetLevel(void)
{
	return giRegisterMedia_CurrentLevel;
}

extern qboolean SND_RegisterAudio_LevelLoadEnd(qboolean bDeleteEverythingNotUsedThisLevel);

void RE_RegisterMedia_LevelLoadEnd(void)
{
	RE_RegisterModels_LevelLoadEnd(qfalse);
	RE_RegisterImages_LevelLoadEnd();
	SND_RegisterAudio_LevelLoadEnd(qfalse);

#ifdef _XBOX
	if (Sys_IsDirectMapBoot())
	{
		gbAllowScreenDissolve = qfalse;
	}
#endif
	if (gbAllowScreenDissolve)
	{
		RE_InitDissolve(qfalse);
	}

	S_RestartMusic();
	
	extern qboolean gbAlreadyDoingLoad;
					gbAlreadyDoingLoad = qfalse;
}




/*
** R_GetModelByHandle
*/
model_t	*R_GetModelByHandle( qhandle_t index ) {
	model_t		*mod;

	// out of range gets the defualt model
	if ( index < 1 || index >= tr.numModels ) {
		return tr.models[0];
	}

	mod = tr.models[index];

	return mod;
}

//===============================================================================

/*
** R_AllocModel
*/
model_t *R_AllocModel( void ) {
	model_t		*mod;

	if ( tr.numModels == MAX_MOD_KNOWN ) {
		return NULL;
	}

	mod = (model_t*) Hunk_Alloc( sizeof( *tr.models[tr.numModels] ), qtrue );
	mod->index= tr.numModels;
	tr.models[tr.numModels] = mod;
	tr.numModels++;

	return mod;
}

/*
Ghoul2 Insert Start
*/

/*
================
return a hash value for the filename
================
*/
static long generateHashValue( const char *fname, const int size ) {
	int		i;
	long	hash;
	char	letter;

	hash = 0;
	i = 0;
	while (fname[i] != '\0') {
		letter = tolower(fname[i]);
		if (letter =='.') break;				// don't include extension
		if (letter =='\\') letter = '/';		// damn path names
		hash+=(long)(letter)*(i+119);
		i++;
	}
	hash &= (size-1);
	return hash;
}

void RE_InsertModelIntoHash(const char *name, model_t *mod)
{
	int			hash;
	modelHash_t	*mh;

	hash = generateHashValue(name, FILE_HASH_SIZE);

	// insert this file into the hash table so we can look it up faster later
	mh = (modelHash_t*)Hunk_Alloc( sizeof( modelHash_t ), qtrue );

	mh->next = mhHashTable[hash];
	mh->handle = mod->index;
	strcpy(mh->name, name);
	mhHashTable[hash] = mh;
}

#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
static void STEFX_InsertModelHandleAliasIntoHash(const char *name, qhandle_t handle)
{
	int			hash;
	modelHash_t	*mh;

	if (!name || handle <= 0)
	{
		return;
	}

	hash = generateHashValue(name, FILE_HASH_SIZE);
	mh = (modelHash_t*)Hunk_Alloc( sizeof( modelHash_t ), qtrue );
	mh->next = mhHashTable[hash];
	mh->handle = handle;
	strcpy(mh->name, name);
	mhHashTable[hash] = mh;
}
#endif

#ifdef STEFX_ELITE_FORCE_SP
static qboolean STEFX_IsMdrModelName(const char *name)
{
	const char *ext = name ? strrchr(name, '.') : NULL;
	return (ext && !Q_stricmp(ext, ".mdr"));
}

#if defined(_XBOX)
static qhandle_t RE_RegisterModel_Actual( const char *name );
#endif

static qboolean STEFX_IsGhoul2ModelName(const char *name)
{
	const char *ext = name ? strrchr(name, '.') : NULL;
	return (ext && (!Q_stricmp(ext, ".glm") || !Q_stricmp(ext, ".gla")));
}

#if defined(_XBOX)
static qboolean STEFX_ShouldUseMdrMemoryPlaceholder(const char *name, int size);
static void STEFX_LogMdrMemoryStats(const char *phase, const char *name, int fileLen, int requestSize, int realSize, int alignPad, int fit);

static qboolean STEFX_IsBorgPlayerModelName(const char *name)
{
	return (name && (strstr(name, "models/players/borg") || strstr(name, "models\\players\\borg")));
}

static qboolean STEFX_IsPlayerModelName(const char *name)
{
	return (name && (strstr(name, "models/players/") || strstr(name, "models\\players\\")));
}

static const char *STEFX_ModelPartToken(const char *name)
{
	if (!name)
	{
		return NULL;
	}
	if (strstr(name, "/lower.") || strstr(name, "\\lower."))
	{
		return "lower.";
	}
	if (strstr(name, "/upper.") || strstr(name, "\\upper."))
	{
		return "upper.";
	}
	if (strstr(name, "/head.") || strstr(name, "\\head."))
	{
		return "head.";
	}
	return NULL;
}

static const char *STEFX_DefaultPlayerMdrFallbackName(const char *name)
{
	const char *part = STEFX_ModelPartToken(name);
	if (!STEFX_IsPlayerModelName(name) || !STEFX_IsMdrModelName(name) || !part)
	{
		return NULL;
	}

	if (!Q_stricmp(part, "lower."))
	{
		return "models/players/hazard/lower.mdr";
	}
	if (!Q_stricmp(part, "upper."))
	{
		return "models/players/hazard/upper.mdr";
	}

	return NULL;
}

static qboolean STEFX_IsDefaultPlayerMdrFallbackName(const char *name)
{
	const char *fallback;

	fallback = STEFX_DefaultPlayerMdrFallbackName(name);
	return (qboolean)(fallback && !Q_stricmp(name, fallback));
}
#endif

static qboolean STEFX_RegisterGhoul2Disabled(model_t *mod, const char *name)
{
	if (!STEFX_IsGhoul2ModelName(name))
	{
		return qfalse;
	}

	mod->type = MOD_BAD;
	RE_InsertModelIntoHash(name, mod);
#ifdef _XBOX
	XBLF("STEFX: Ghoul2 model disabled '%s'", name ? name : "(null)");
#endif
	return qtrue;
}

static qhandle_t STEFX_RegisterMdrPlaceholderIfPresent(model_t *mod, const char *name)
{
	fileHandle_t f = 0;
	unsigned int ident = 0;
	int len;
	int read;

	if (!STEFX_IsMdrModelName(name))
	{
		return 0;
	}

	len = FS_FOpenFileByMode(name, &f, FS_READ);
	if (len < 4 || !f)
	{
#ifdef _XBOX
		XBLF("EF: RE_RegisterModel MDR probe missing '%s' len=%d handle=%d", name ? name : "(null)", len, f);
#endif
		if (f)
		{
			FS_FCloseFile(f);
		}
		return 0;
	}

#if defined(_XBOX)
	if (STEFX_IsPlayerModelName(name) && !STEFX_IsBorgPlayerModelName(name))
	{
		const char *fallbackName = STEFX_DefaultPlayerMdrFallbackName(name);
		int requestSize = len + 1;
		int realSize = 0;
		int alignPad = 0;
		int largestFreeBlock = 0;
		qboolean wouldFit = Z_WouldAllocFit(requestSize, TAG_MODEL_MD3, 32, &realSize, &alignPad, &largestFreeBlock);

		STEFX_LogMdrMemoryStats("preflight", name, len, requestSize, realSize, alignPad, wouldFit ? 1 : 0);

		if (!wouldFit && fallbackName && !STEFX_IsDefaultPlayerMdrFallbackName(name))
		{
			FS_FCloseFile(f);
			mod->type = MOD_BAD;
			RE_InsertModelIntoHash(name, mod);
			XBLF("STEFX: RE_RegisterModel MDR player part cannot fit '%s' len=%d request=%d real=%d largest=%d shortfall=%d fallbackSuppressed='%s'; inserted MOD_BAD",
				name,
				len,
				requestSize,
				realSize,
				largestFreeBlock,
				(realSize > largestFreeBlock) ? (realSize - largestFreeBlock) : 0,
				fallbackName);
			return mod->index;
		}

		if (!wouldFit)
		{
			FS_FCloseFile(f);
			mod->type = MOD_BAD;
			RE_InsertModelIntoHash(name, mod);
			XBLF("STEFX: RE_RegisterModel MDR default player model cannot fit '%s' len=%d request=%d real=%d largest=%d shortfall=%d; inserted MOD_BAD",
				name,
				len,
				requestSize,
				realSize,
				largestFreeBlock,
				(realSize > largestFreeBlock) ? (realSize - largestFreeBlock) : 0);
			return mod->index;
		}
	}

	if (!STEFX_ShouldUseMdrMemoryPlaceholder(name, len))
	{
#ifdef _XBOX
		if (name && (strstr(name, "models/players/borg") || strstr(name, "models\\players\\borg")))
		{
			XBLF("STEFX: RE_RegisterModel Borg MDR placeholder bypass '%s' len=%d", name, len);
		}
#endif
		FS_FCloseFile(f);
		return 0;
	}
#else
	FS_FCloseFile(f);
	return 0;
#endif

	read = FS_Read(&ident, 4, f);
	FS_FCloseFile(f);
	ident = LittleLong(ident);

	if (read != 4 || ident != MD4_IDENT)
	{
#ifdef _XBOX
		XBLF("EF: RE_RegisterModel MDR probe rejected '%s' read=%d ident=0x%08x", name, read, ident);
#endif
		return 0;
	}

	mod->type = MOD_STEFX_MDR_PLACEHOLDER;
	mod->dataSize += len;
	mod->numLods = 1;
	RE_InsertModelIntoHash(name, mod);
#ifdef _XBOX
	XBLF("EF: RE_RegisterModel accepted MDR placeholder '%s' handle=%d len=%d", name, mod->index, len);
#endif
	return mod->index;
}

#if defined(_XBOX)
static qboolean STEFX_ShouldUseMdrMemoryPlaceholder(const char *name, int size)
{
	const int overCapLimit = 1536 * 1024;

	if (size <= (1536 * 1024))
	{
		return qfalse;
	}

	if (STEFX_IsBorgPlayerModelName(name))
	{
		return qfalse;
	}

	if (STEFX_IsPlayerModelName(name))
	{
#ifdef _XBOX
		XBLF("STEFX: R_LoadMDR allowing exact player model '%s' size=%d cap=%d",
			name ? name : "(null)", size, overCapLimit);
#endif
		return qfalse;
	}

#ifdef _XBOX
	XBLF("STEFX: R_LoadMDR budget placeholder model '%s' size=%d over cap=%d",
		name ? name : "(null)", size, overCapLimit);
#endif
	return qtrue;
}

static void STEFX_LogMdrMemoryStats(const char *phase, const char *name, int fileLen, int requestSize, int realSize, int alignPad, int fit)
{
	zmemstats_t stats;
	int shortfall;

	Z_GetMemoryStats(&stats);
	shortfall = (realSize > stats.largestFreeBlock) ? (realSize - stats.largestFreeBlock) : 0;

	XBLF("STEFX: MDR memory %s model='%s' fileLen=%d request=%d real=%d alignPad=%d fit=%d shortfall=%d zoneSize=%d used=%d overhead=%d free=%d largest=%d freeBlocks=%d peak=%d md3=%d glm=%d gla=%d bsp=%d sndRaw=%d filesys=%d",
		phase ? phase : "(null)",
		name ? name : "(null)",
		fileLen,
		requestSize,
		realSize,
		alignPad,
		fit,
		shortfall,
		stats.zoneSize,
		stats.usedBytes,
		stats.overheadBytes,
		stats.freeBytes,
		stats.largestFreeBlock,
		stats.freeBlocks,
		stats.peakBytes,
		stats.modelMd3Bytes,
		stats.modelGlmBytes,
		stats.modelGlaBytes,
		stats.bspBytes,
		stats.soundRawBytes,
		stats.filesysBytes);
}
#endif
#endif
/*
Ghoul2 Insert End
*/


/*
====================
RE_RegisterModel

Loads in a model for the given name

Zero will be returned if the model fails to load.
An entry will be retained for failed models as an
optimization to prevent disk rescanning if they are
asked for again.
====================
*/
static qhandle_t RE_RegisterModel_Actual( const char *name ) 
{
	model_t		*mod;
	unsigned	*buf;
	int			lod;
	int			ident;
	qboolean	loaded;
//	qhandle_t	hModel;
	int			numLoaded;
/*
Ghoul2 Insert Start
*/
	int			hash;
	modelHash_t	*mh;
/*
Ghoul2 Insert End
*/

	if ( !name || !name[0] ) {
		VID_Printf( PRINT_WARNING, "RE_RegisterModel: NULL name\n" );
		return 0;
	}

	if ( strlen( name ) >= MAX_QPATH ) {
		VID_Printf( PRINT_DEVELOPER, "Model name exceeds MAX_QPATH\n" );
		return 0;
	}

/*
Ghoul2 Insert Start
*/
//	if (!tr.registered) {
//		VID_Printf( PRINT_WARNING, "RE_RegisterModel (%s) called before ready!\n",name );
//		return 0;
//	}
	//
	// search the currently loaded models
	//

	hash = generateHashValue(name, FILE_HASH_SIZE);

	//
	// see if the model is already loaded
	//
	for (mh=mhHashTable[hash]; mh; mh=mh->next) {
		if (Q_stricmp(mh->name, name) == 0) {
			if (tr.models[mh->handle]->type == MOD_BAD)
			{
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
				if (STEFX_IsBorgPlayerModelName(name))
				{
					XBLF("STEFX: RE_RegisterModel Borg MOD_BAD cache hit exact required '%s' handle=%d", name, mh->handle);
				}
#endif
				return 0;
			}
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
			if (STEFX_IsBorgPlayerModelName(name))
			{
				XBLF("STEFX: RE_RegisterModel Borg cache hit '%s' handle=%d type=%d", name, mh->handle, tr.models[mh->handle]->type);
			}
#endif
			return mh->handle;
		}
	}

#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	if (STEFX_IsBorgPlayerModelName(name))
	{
		XBLF("STEFX: RE_RegisterModel Borg exact load required '%s'", name);
	}
#endif

/*
Ghoul2 Insert End
*/

	if (name[0] == '#')
	{
		char		temp[MAX_QPATH];

		tr.numBSPModels++;
#ifndef DEDICATED
		RE_LoadWorldMap_Actual(va("maps/%s.bsp", name + 1), tr.bspModels[tr.numBSPModels - 1], tr.numBSPModels);	//this calls R_LoadSubmodels which will put them into the Hash
#endif
		Com_sprintf(temp, MAX_QPATH, "*%d-0", tr.numBSPModels);
		hash = generateHashValue(temp, FILE_HASH_SIZE);
		for (mh=mhHashTable[hash]; mh; mh=mh->next) 
		{
			if (Q_stricmp(mh->name, temp) == 0) 
			{
				return mh->handle;
			}
		}
		
		return 0;
	}

	// allocate a new model_t

	if ( ( mod = R_AllocModel() ) == NULL ) {
		VID_Printf( PRINT_WARNING, "RE_RegisterModel: R_AllocModel() failed for '%s'\n", name);
		return 0;
	}

	// only set the name after the model has been successfully loaded
	Q_strncpyz( mod->name, name, sizeof( mod->name ) );

#ifdef STEFX_ELITE_FORCE_SP
	if (STEFX_RegisterGhoul2Disabled(mod, name))
	{
		return 0;
	}

#if defined(_XBOX)
	qhandle_t stefxMdrPreflightHandle = STEFX_RegisterMdrPlaceholderIfPresent(mod, name);
	if (stefxMdrPreflightHandle)
	{
		return stefxMdrPreflightHandle;
	}
#endif

#endif

	// make sure the render thread is stopped
	//R_SyncRenderThread();

	int iLODStart = 0;
	if (strstr (name, ".md3")) {
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
		iLODStart = 0;
#else
		iLODStart = MD3_MAX_LODS-1;	//this loads the md3s in reverse so they can be biased
#endif
	}
	mod->numLods = 0;

	//
	// load the files
	//
	numLoaded = 0;

	for ( lod = iLODStart; lod >= 0 ; lod-- ) {
		char filename[1024];

		strcpy( filename, name );

		if ( lod != 0 ) {
			char namebuf[80];

			if ( strrchr( filename, '.' ) ) {
				*strrchr( filename, '.' ) = 0;
			}
			sprintf( namebuf, "_%d.md3", lod );
			strcat( filename, namebuf );
		}

		qboolean bAlreadyCached = qfalse;		
		if (!RE_RegisterModels_GetDiskFile(filename, (void **)&buf, &bAlreadyCached))
		{
			if (numLoaded)	//we loaded one already, but a higher LOD is missing!
			{
				Com_Error (ERR_DROP, "R_LoadMD3: %s has LOD %d but is missing LOD %d ('%s')!", mod->name, lod+1, lod, filename);
			}
			continue;
		}
		
		//loadmodel = mod;	// this seems to be fairly pointless

		// important that from now on we pass 'filename' instead of 'name' to all model load functions,
		//	because 'filename' accounts for any LOD mangling etc so guarantees unique lookups for yet more
		//	internal caching...
		//		
		ident = *(unsigned *)buf;
		if (!bAlreadyCached)
		{
			ident = LittleLong(ident);
		}

		switch (ident)
		{
			// if you add any new types of model load in this switch-case, tell me, 
			//	or copy what I've done with the cache scheme (-ste).
			//
			case MDXA_IDENT:

				loaded = R_LoadMDXA( mod, buf, filename, bAlreadyCached );
				break;
		
			case MDXM_IDENT:
				
				loaded = R_LoadMDXM( mod, buf, filename, bAlreadyCached );
				break;

			case MD3_IDENT:

				loaded = R_LoadMD3( mod, lod, buf, filename, bAlreadyCached );
				break;

#ifdef STEFX_ELITE_FORCE_SP
			case MD4_IDENT:
#if defined(_XBOX)
				if ( strstr( filename, "models/players/borg" ) || strstr( filename, "models\\players\\borg" ) )
				{
					XBLF( "STEFX: RE_RegisterModel loading Borg MDR '%s' cached=%d", filename, bAlreadyCached ? 1 : 0 );
				}
#endif
				loaded = R_LoadMDR( mod, buf, filename, bAlreadyCached );
				break;
#endif

			default:

				VID_Printf (PRINT_WARNING,"RE_RegisterModel: unknown fileid for %s\n", filename);
				goto fail;
		}
		
		if (!bAlreadyCached){	// important to check!!
			FS_FreeFile (buf);
		}

		if ( !loaded ) {
			if ( lod == 0 ) {
				VID_Printf (PRINT_WARNING,"RE_RegisterModel: cannot load %s\n", filename);
				goto fail;
			} else {
				break;
			}
		} else {
			mod->numLods++;
			numLoaded++;
			// if we have a valid model and are biased
			// so that we won't see any higher detail ones,
			// stop loading them
			if ( lod <= r_lodbias->integer ) {
				break;
			}
		}
	}

	if ( numLoaded ) {
		// duplicate into higher lod spots that weren't
		// loaded, in case the user changes r_lodbias on the fly
		for ( lod-- ; lod >= 0 ; lod-- ) {
			mod->numLods++;
			mod->md3[lod] = mod->md3[lod+1];
		}
/*
Ghoul2 Insert Start
*/

	RE_InsertModelIntoHash(name, mod);
	return mod->index;
/*
Ghoul2 Insert End
*/
	
	}


fail:
	// we still keep the model_t around, so if the model name is asked for
	// again, we won't bother scanning the filesystem
	mod->type = MOD_BAD;
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	if (STEFX_IsPlayerModelName(name))
	{
		const char *fallbackName = STEFX_DefaultPlayerMdrFallbackName(name);
		if (fallbackName && !STEFX_IsDefaultPlayerMdrFallbackName(name))
		{
			qhandle_t fallback = RE_RegisterModel_Actual(fallbackName);
			if (fallback)
			{
				STEFX_InsertModelHandleAliasIntoHash(name, fallback);
				XBLF("STEFX: RE_RegisterModel MDR hazard fallback '%s' index=%d -> '%s' handle=%d numLoaded=%d",
					name, mod->index, fallbackName, fallback, numLoaded);
				return fallback;
			}
			XBLF("STEFX: RE_RegisterModel MDR hazard fallback failed '%s' index=%d fallback='%s' numLoaded=%d",
				name, mod->index, fallbackName, numLoaded);
		}
		else if (fallbackName)
		{
			XBLF("STEFX: RE_RegisterModel MDR hazard fallback base failed '%s' index=%d numLoaded=%d",
				name, mod->index, numLoaded);
		}
		XBLF("STEFX: RE_RegisterModel player fail insert MOD_BAD '%s' index=%d numLoaded=%d", name, mod->index, numLoaded);
	}
#endif
	RE_InsertModelIntoHash(name, mod);
	return 0;
}




// wrapper function needed to avoid problems with mid-function returns so I can safely use this bool to tell the
//	z_malloc-fail recovery code whether it's safe to ditch any model caches...
//
qboolean gbInsideRegisterModel = qfalse;
qhandle_t RE_RegisterModel( const char *name )
{
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	qboolean stefxBorgModelTrace = (name && (strstr(name, "models/players/borg") || strstr(name, "models\\players\\borg")));
	if (stefxBorgModelTrace)
	{
		XBLF("STEFX: RE_RegisterModel Borg entry '%s'", name);
	}
#endif
	gbInsideRegisterModel = qtrue;	// !!!!!!!!!!!!!!

		qhandle_t q = RE_RegisterModel_Actual( name );

if (!name || strlen(name) < 4 || stricmp(&name[strlen(name)-4],".gla")){
	gbInsideRegisterModel = qfalse;		// GLA files recursively call this, so don't turn off half way. A reference count would be nice, but if any ERR_DROP ever occurs within the load then the refcount will be knackered from then on
}

#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	if (stefxBorgModelTrace)
	{
		XBLF("STEFX: RE_RegisterModel Borg exit '%s' -> %d", name, q);
	}
#endif
	return q;
}


/*
=================
R_LoadMDR
=================
*/
#ifdef STEFX_ELITE_FORCE_SP
static qboolean R_LoadMDR (model_t *mod, void *buffer, const char *mod_name, qboolean &bAlreadyCached ) {
	int					i, j;
	md4Header_t			*pinmodel;
	md4LOD_t			*lod;
	md4Surface_t		*surf;
	int					version;
	int					size;

	pinmodel = (md4Header_t *)buffer;
	version = pinmodel->version;
	size = pinmodel->ofsEnd;

	if (!bAlreadyCached)
	{
		version = LittleLong(version);
		size = LittleLong(size);
	}

	if (version != MD4_VERSION)
	{
		VID_Printf( PRINT_WARNING, "R_LoadMDR: %s has wrong version (%i should be %i)\n",
			mod_name, version, MD4_VERSION );
#if defined(_XBOX)
		XBLF("STEFX: R_LoadMDR wrong version '%s' version=%d expected=%d", mod_name, version, MD4_VERSION);
#endif
		return qfalse;
	}

	if (size <= 0)
	{
		VID_Printf( PRINT_WARNING, "R_LoadMDR: %s has invalid size %i\n", mod_name, size );
#if defined(_XBOX)
		XBLF("STEFX: R_LoadMDR invalid size '%s' size=%d", mod_name, size);
#endif
		return qfalse;
	}

#if defined(_XBOX)
	if (STEFX_IsPlayerModelName(mod_name))
	{
		STEFX_LogMdrMemoryStats("load-start", mod_name, size, size, size, 0, 1);
	}

	if (STEFX_ShouldUseMdrMemoryPlaceholder(mod_name, size))
	{
		mod->type = MOD_STEFX_MDR_PLACEHOLDER;
		mod->dataSize += size;
		mod->numLods = 0;
		mod->md4 = NULL;
		XBLF("STEFX: R_LoadMDR overbudget placeholder '%s' size=%d cap=%d",
			mod_name, size, 1536 * 1024);
		return qtrue;
	}
#endif

	mod->type = MOD_MDR;
	mod->dataSize += size;

	qboolean bAlreadyFound = qfalse;
	mod->md4 = (md4Header_t *)RE_RegisterModels_Malloc(size, buffer, mod_name, &bAlreadyFound, TAG_MODEL_MD3);
	if (!mod->md4)
	{
#if defined(_XBOX)
		XBLF("STEFX: R_LoadMDR allocation failed '%s' size=%d", mod_name, size);
#endif
		return qfalse;
	}

#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	if (!bAlreadyFound && mod->md4 == buffer)
	{
		bAlreadyCached = qtrue;
		XBLF("STEFX: R_LoadMDR using adopted disk buffer '%s' size=%d", mod_name, size);
	}
#else
	assert(bAlreadyCached == bAlreadyFound);
#endif

	if (!bAlreadyFound)
	{
#ifdef _XBOX
		if (mod->md4 != buffer)
		{
			memcpy(mod->md4, buffer, size);
		}
#else
		bAlreadyCached = qtrue;
		assert(mod->md4 == buffer);
#endif

		LL(mod->md4->ident);
		LL(mod->md4->version);
		LL(mod->md4->numFrames);
		LL(mod->md4->numBones);
		LL(mod->md4->ofsFrames);
		LL(mod->md4->numLODs);
		LL(mod->md4->ofsLODs);
		LL(mod->md4->numTags);
		LL(mod->md4->ofsTags);
		LL(mod->md4->ofsEnd);

		if (mod->md4->numFrames < 1 || mod->md4->numBones < 1 || mod->md4->numLODs < 1)
		{
			VID_Printf( PRINT_WARNING, "R_LoadMDR: %s has invalid counts frames=%i bones=%i lods=%i\n",
				mod_name, mod->md4->numFrames, mod->md4->numBones, mod->md4->numLODs );
			return qfalse;
		}

		if (mod->md4->numBones > MD4_MAX_BONES)
		{
			VID_Printf( PRINT_WARNING, "R_LoadMDR: %s has too many bones (%i > %i)\n",
				mod_name, mod->md4->numBones, MD4_MAX_BONES );
			return qfalse;
		}

		md4Tag_t *tag = (md4Tag_t *)((byte *)mod->md4 + mod->md4->ofsTags);
		for (i = 0; i < mod->md4->numTags; i++, tag++)
		{
			LL(tag->boneIndex);
			if (tag->boneIndex < 0 || tag->boneIndex >= mod->md4->numBones)
			{
				VID_Printf( PRINT_WARNING, "R_LoadMDR: %s tag %s has invalid bone index %i of %i\n",
					mod_name, tag->name, tag->boneIndex, mod->md4->numBones );
#ifdef _XBOX
				XBLF("STEFX: R_LoadMDR invalid tag model='%s' tag='%s' bone=%d bones=%d",
					mod_name, tag->name, tag->boneIndex, mod->md4->numBones);
#endif
			}
		}

		lod = (md4LOD_t *)((byte *)mod->md4 + mod->md4->ofsLODs);
		for (i = 0; i < mod->md4->numLODs; i++)
		{
			LL(lod->numSurfaces);
			LL(lod->ofsSurfaces);
			LL(lod->ofsEnd);

			surf = (md4Surface_t *)((byte *)lod + lod->ofsSurfaces);
			for (j = 0; j < lod->numSurfaces; j++)
			{
				shader_t *sh;

				LL(surf->ofsHeader);
				LL(surf->numVerts);
				LL(surf->ofsVerts);
				LL(surf->numTriangles);
				LL(surf->ofsTriangles);
				LL(surf->numBoneReferences);
				LL(surf->ofsBoneReferences);
				LL(surf->ofsEnd);

				if (surf->numVerts > SHADER_MAX_VERTEXES)
				{
					Com_Error( ERR_DROP, "R_LoadMDR: %s has more than %i verts on a surface (%i)",
						mod_name, SHADER_MAX_VERTEXES, surf->numVerts );
				}
				if (surf->numTriangles * 3 > SHADER_MAX_INDEXES)
				{
					Com_Error( ERR_DROP, "R_LoadMDR: %s has more than %i triangles on a surface (%i)",
						mod_name, SHADER_MAX_INDEXES / 3, surf->numTriangles );
				}

				surf->ident = SF_MDR;
				Q_strlwr(surf->name);

				sh = R_FindShader(surf->shader, lightmapsNone, stylesDefault, qtrue);
				if (sh->defaultShader)
				{
					surf->shaderIndex = 0;
				}
				else
				{
					surf->shaderIndex = sh->index;
				}
				RE_RegisterModels_StoreShaderRequest(mod_name, &surf->shader[0], &surf->shaderIndex);

				surf = (md4Surface_t *)((byte *)surf + surf->ofsEnd);
			}

			lod = (md4LOD_t *)((byte *)lod + lod->ofsEnd);
		}
	}

	mod->numLods = (mod->md4->numLODs > 0) ? (unsigned char)(mod->md4->numLODs - 1) : 0;
#ifdef _XBOX
	XBLF("STEFX: R_LoadMDR loaded '%s' frames=%d bones=%d lods=%d size=%d",
		mod_name, mod->md4->numFrames, mod->md4->numBones, mod->md4->numLODs, size);
	if (STEFX_IsPlayerModelName(mod_name))
	{
		STEFX_LogMdrMemoryStats("load-done", mod_name, size, size, size, 0, 1);
	}
#endif
	return qtrue;
}
#endif


/*
=================
R_LoadMD3
=================
*/
static qboolean R_LoadMD3 (model_t *mod, int lod, void *buffer, const char *mod_name, qboolean &bAlreadyCached ) {
	int					i, j;
	md3Header_t			*pinmodel;
	md3Surface_t		*surf;
	md3Shader_t			*shader;
	int					version;
	int					size;

#ifndef _M_IX86
	md3Frame_t			*frame;
	md3Triangle_t		*tri;
	md3St_t				*st;
	md3XyzNormal_t		*xyz;
	md3Tag_t			*tag;
#endif


	pinmodel= (md3Header_t *)buffer;
	//
	// read some fields from the binary, but only LittleLong() them when we know this wasn't an already-cached model...
	//
	version = pinmodel->version;
	size	= pinmodel->ofsEnd;

	if (!bAlreadyCached)
	{
		version = LittleLong(version);
		size	= LittleLong(size);
	}
	
	if (version != MD3_VERSION) {
		VID_Printf( PRINT_WARNING, "R_LoadMD3: %s has wrong version (%i should be %i)\n",
				 mod_name, version, MD3_VERSION);
		return qfalse;
	}

	mod->type      = MOD_MESH;	
	mod->dataSize += size;

	qboolean bAlreadyFound = qfalse;
	mod->md3[lod] = (md3Header_t *) RE_RegisterModels_Malloc(size, buffer, mod_name, &bAlreadyFound, TAG_MODEL_MD3);
	if (!mod->md3[lod])
	{
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
		XBLF("STEFX: R_LoadMD3 allocation failed '%s' lod=%d size=%d", mod_name, lod, size);
#endif
		return qfalse;
	}

	assert(bAlreadyCached == bAlreadyFound);

	if (!bAlreadyFound)
	{	
		// horrible new hackery, if !bAlreadyFound then we've just done a tag-morph, so we need to set the 
		//	bool reference passed into this function to true, to tell the caller NOT to do an FS_Freefile since
		//	we've hijacked that memory block...
		//
		// Aaaargh. Kill me now...
		//
#ifdef _XBOX
		memcpy( mod->md3[lod], buffer, size );
#else
		bAlreadyCached = qtrue;
		assert( mod->md3[lod] == buffer );
#endif

		LL(mod->md3[lod]->ident);
		LL(mod->md3[lod]->version);
		LL(mod->md3[lod]->numFrames);
		LL(mod->md3[lod]->numTags);
		LL(mod->md3[lod]->numSurfaces);
		LL(mod->md3[lod]->ofsFrames);
		LL(mod->md3[lod]->ofsTags);
		LL(mod->md3[lod]->ofsSurfaces);
		LL(mod->md3[lod]->ofsEnd);
	}

	if ( mod->md3[lod]->numFrames < 1 ) {
		VID_Printf( PRINT_WARNING, "R_LoadMD3: %s has no frames\n", mod_name );
		return qfalse;
	}

	if (bAlreadyFound)
	{
		return qtrue;	// All done. Stop, go no further, do not pass Go...
	}

#ifndef _M_IX86
	//
	// optimisation, we don't bother doing this for standard intel case since our data's already in that format...
	//

	// swap all the frames
    frame = (md3Frame_t *) ( (byte *)mod->md3[lod] + mod->md3[lod]->ofsFrames );
    for ( i = 0 ; i < mod->md3[lod]->numFrames ; i++, frame++) {
    	frame->radius = LittleFloat( frame->radius );
        for ( j = 0 ; j < 3 ; j++ ) {
            frame->bounds[0][j] = LittleFloat( frame->bounds[0][j] );
            frame->bounds[1][j] = LittleFloat( frame->bounds[1][j] );
	    	frame->localOrigin[j] = LittleFloat( frame->localOrigin[j] );
        }
	}

	// swap all the tags
    tag = (md3Tag_t *) ( (byte *)mod->md3[lod] + mod->md3[lod]->ofsTags );
    for ( i = 0 ; i < mod->md3[lod]->numTags * mod->md3[lod]->numFrames ; i++, tag++) {
        for ( j = 0 ; j < 3 ; j++ ) {
			tag->origin[j] = LittleFloat( tag->origin[j] );
			tag->axis[0][j] = LittleFloat( tag->axis[0][j] );
			tag->axis[1][j] = LittleFloat( tag->axis[1][j] );
			tag->axis[2][j] = LittleFloat( tag->axis[2][j] );
        }
	}
#endif

	// swap all the surfaces
	surf = (md3Surface_t *) ( (byte *)mod->md3[lod] + mod->md3[lod]->ofsSurfaces );
	for ( i = 0 ; i < mod->md3[lod]->numSurfaces ; i++) {
        LL(surf->flags);
        LL(surf->numFrames);
        LL(surf->numShaders);
        LL(surf->numTriangles);
        LL(surf->ofsTriangles);
        LL(surf->numVerts);
        LL(surf->ofsShaders);
        LL(surf->ofsSt);
        LL(surf->ofsXyzNormals);
        LL(surf->ofsEnd);
		
		if ( surf->numVerts > SHADER_MAX_VERTEXES ) {
			Com_Error (ERR_DROP, "R_LoadMD3: %s has more than %i verts on a surface (%i)",
				mod_name, SHADER_MAX_VERTEXES, surf->numVerts );
		}
		if ( surf->numTriangles*3 > SHADER_MAX_INDEXES ) {
			Com_Error (ERR_DROP, "R_LoadMD3: %s has more than %i triangles on a surface (%i)",
				mod_name, SHADER_MAX_INDEXES / 3, surf->numTriangles );
		}
	
		// change to surface identifier
		surf->ident = SF_MD3;

		// lowercase the surface name so skin compares are faster
		Q_strlwr( surf->name );

		// strip off a trailing _1 or _2
		// this is a crutch for q3data being a mess
		j = strlen( surf->name );
		if ( j > 2 && surf->name[j-2] == '_' ) {
			surf->name[j-2] = 0;
		}

        // register the shaders
        shader = (md3Shader_t *) ( (byte *)surf + surf->ofsShaders );
        for ( j = 0 ; j < surf->numShaders ; j++, shader++ ) {
            shader_t	*sh;

            sh = R_FindShader( shader->name, lightmapsNone, stylesDefault, qtrue );
			if ( sh->defaultShader ) {
				shader->shaderIndex = 0;
			} else {
				shader->shaderIndex = sh->index;
			}
			RE_RegisterModels_StoreShaderRequest(mod_name, &shader->name[0], &shader->shaderIndex);
        }


#ifndef _M_IX86
//
// optimisation, we don't bother doing this for standard intel case since our data's already in that format...
//

		// swap all the triangles
		tri = (md3Triangle_t *) ( (byte *)surf + surf->ofsTriangles );
		for ( j = 0 ; j < surf->numTriangles ; j++, tri++ ) {
			LL(tri->indexes[0]);
			LL(tri->indexes[1]);
			LL(tri->indexes[2]);
		}

		// swap all the ST
        st = (md3St_t *) ( (byte *)surf + surf->ofsSt );
        for ( j = 0 ; j < surf->numVerts ; j++, st++ ) {
            st->st[0] = LittleFloat( st->st[0] );
            st->st[1] = LittleFloat( st->st[1] );
        }

		// swap all the XyzNormals
        xyz = (md3XyzNormal_t *) ( (byte *)surf + surf->ofsXyzNormals );
        for ( j = 0 ; j < surf->numVerts * surf->numFrames ; j++, xyz++ ) 
		{
            xyz->xyz[0] = LittleShort( xyz->xyz[0] );
            xyz->xyz[1] = LittleShort( xyz->xyz[1] );
            xyz->xyz[2] = LittleShort( xyz->xyz[2] );

            xyz->normal = LittleShort( xyz->normal );
        }
#endif

		// find the next surface
		surf = (md3Surface_t *)( (byte *)surf + surf->ofsEnd );
	}
    
	return qtrue;
}


//=============================================================================

void ShaderTableCleanup();
void CM_LoadShaderText(bool forceReload);
void CM_SetupShaderProperties(void);

void R_HunkClearCrap(void)
{
	ShaderTableCleanup();
	tr.numModels = 0;
	memset(tr.models, 0, sizeof(tr.models));
	tr.numShaders = 0;
	tr.numSkins = 0;
}

/*
** RE_BeginRegistration
*/
void RE_BeginRegistration( glconfig_t *glconfigOut ) {
#ifndef _XBOX
	ShaderTableCleanup();
#endif
	Hunk_ClearToMark();

	R_Init();
	*glconfigOut = glConfig;

	tr.viewCluster = -1;		// force markleafs to regenerate
	RE_ClearScene();
	tr.registered = qtrue;

	R_SyncRenderThread();
}

//=============================================================================

/*
===============
R_ModelInit
===============
*/
void R_ModelInit( void ) 
{
#ifdef _XBOX
	// Sorry Raven, but static maps == fragmentation
	if (!CachedModels)
	{
		CachedModels = new CachedModels_t;
	}
#else
	static CachedModels_t singleton;	// sorry vv, your dynamic allocation was a (false) memory leak
	CachedModels = &singleton;
#endif

	model_t		*mod;

	// leave a space for NULL model
	tr.numModels = 0;

	mod = R_AllocModel();
	mod->type = MOD_BAD;
/*
Ghoul2 Insert Start
*/

	memset(mhHashTable, 0, sizeof(mhHashTable));
/*
Ghoul2 Insert End
*/

}


/*
================
R_Modellist_f
================
*/
void R_Modellist_f( void ) {
	int		i, j;
	model_t	*mod;
	int		total;
	int		lods;

	total = 0;
	for ( i = 1 ; i < tr.numModels; i++ ) {
		mod = tr.models[i];
		switch (mod->type)
		{
			default:
				assert(0);
				VID_Printf( PRINT_ALL, "UNKNOWN  :      %s\n", mod->name );
				break;

			case MOD_BAD:
				VID_Printf( PRINT_ALL, "MOD_BAD  :      %s\n", mod->name );
				break;

			case MOD_BRUSH:
				VID_Printf( PRINT_ALL, "%8i : (%i) %s\n", mod->dataSize, mod->numLods, mod->name );
				break;

#ifdef STEFX_ELITE_FORCE_SP
			case MOD_MDR:
				VID_Printf( PRINT_ALL, "%8i : (%i MDR) %s\n", mod->dataSize, mod->numLods, mod->name );
				break;

			case MOD_STEFX_MDR_PLACEHOLDER:
				VID_Printf( PRINT_ALL, "%8i : (MDR placeholder) %s\n", mod->dataSize, mod->name );
				break;
#endif

			case MOD_MDXA:

				VID_Printf( PRINT_ALL, "%8i : (%i) %s\n", mod->dataSize, mod->numLods, mod->name );								
				break;
		
			case MOD_MDXM:
				
				VID_Printf( PRINT_ALL, "%8i : (%i) %s\n", mod->dataSize, mod->numLods, mod->name );								
				break;

			case MOD_MESH:

				lods = 1;
				for ( j = 1 ; j < MD3_MAX_LODS ; j++ ) {
					if ( mod->md3[j] && mod->md3[j] != mod->md3[j-1] ) {
						lods++;
					}
				}				
				VID_Printf( PRINT_ALL, "%8i : (%i) %s\n",mod->dataSize, lods, mod->name );
				break;		
		}
		total += mod->dataSize;
	}
	VID_Printf( PRINT_ALL, "%8i : Total models\n", total );

/*	this doesn't work with the new hunks
	if ( tr.world ) {
		VID_Printf( PRINT_ALL, "%8i : %s\n", tr.world->dataSize, tr.world->name );
	} */
}

//=============================================================================


/*
================
R_GetTag for MD3s
================
*/
static md3Tag_t *R_GetTag( md3Header_t *mod, int frame, const char *tagName ) {
	md3Tag_t		*tag;
	int				i;

	if ( frame >= mod->numFrames ) {
		// it is possible to have a bad frame while changing models, so don't error
		frame = mod->numFrames - 1;
	}

	tag = (md3Tag_t *)((byte *)mod + mod->ofsTags) + frame * mod->numTags;
	for ( i = 0 ; i < mod->numTags ; i++, tag++ ) {
		if ( !strcmp( tag->name, tagName ) ) {
			return tag;	// found it
		}
	}

	return NULL;
}

#ifdef STEFX_ELITE_FORCE_SP
static md4Tag_t *R_STEFX_GetMDRTag( md4Header_t *mod, const char *tagName ) {
	md4Tag_t		*tag;
	int				i;

	if ( !mod || !tagName || mod->numTags <= 0 ) {
		return NULL;
	}

	tag = (md4Tag_t *)((byte *)mod + mod->ofsTags);
	for ( i = 0 ; i < mod->numTags ; i++, tag++ ) {
		if ( !strcmp( tag->name, tagName ) ) {
			return tag;
		}
	}

	return NULL;
}

static qboolean R_STEFX_GetMDRBone( md4Header_t *mod, int frame, int boneIndex, md4Bone_t *bone ) {
	int				frameSize;

	if ( !mod || !bone || boneIndex < 0 || boneIndex >= mod->numBones || mod->numFrames <= 0 ) {
		return qfalse;
	}

	if ( frame < 0 ) {
		frame = 0;
	} else if ( frame >= mod->numFrames ) {
		frame = mod->numFrames - 1;
	}

	if ( mod->ofsFrames < 0 ) {
		md4CompFrame_t	*cframe;

		frameSize = (int)( &((md4CompFrame_t *)0)->bones[ mod->numBones ] );
		cframe = (md4CompFrame_t *)((byte *)mod - mod->ofsFrames + frame * frameSize );
		MC_UnCompress( bone->matrix, cframe->bones[ boneIndex ].Comp );
	} else {
		md4Frame_t		*md4Frame;

		frameSize = (int)( &((md4Frame_t *)0)->bones[ mod->numBones ] );
		md4Frame = (md4Frame_t *)((byte *)mod + mod->ofsFrames + frame * frameSize );
		*bone = md4Frame->bones[ boneIndex ];
	}

	return qtrue;
}

static qboolean R_STEFX_LerpMDRTag( orientation_t *tag, model_t *model, int startFrame, int endFrame,
									float frac, const char *tagName ) {
	md4Tag_t		*mdrTag;
	md4Bone_t		startBone;
	md4Bone_t		finishBone;
	int				axis;
	int				component;
	float			frontLerp;
	float			backLerp;

	if ( !tag || !model || !model->md4 ) {
		return qfalse;
	}

	mdrTag = R_STEFX_GetMDRTag( model->md4, tagName );
	if ( !mdrTag ) {
#if defined(_XBOX)
		static int s_missingTagLogBudget = 0;
		if ( s_missingTagLogBudget < 24 ) {
			XBLF( "STEFX: R_LerpTag MDR missing tag='%s' model='%s' tags=%d",
				tagName ? tagName : "(null)",
				model->name,
				model->md4->numTags );
			s_missingTagLogBudget++;
		}
#endif
		return qfalse;
	}

	if ( !R_STEFX_GetMDRBone( model->md4, startFrame, mdrTag->boneIndex, &startBone ) ||
		 !R_STEFX_GetMDRBone( model->md4, endFrame, mdrTag->boneIndex, &finishBone ) ) {
#if defined(_XBOX)
		static int s_badTagLogBudget = 0;
		if ( s_badTagLogBudget < 24 ) {
			XBLF( "STEFX: R_LerpTag MDR bad bone tag='%s' model='%s' bone=%d bones=%d frames=%d/%d count=%d",
				tagName ? tagName : "(null)",
				model->name,
				mdrTag->boneIndex,
				model->md4->numBones,
				startFrame,
				endFrame,
				model->md4->numFrames );
			s_badTagLogBudget++;
		}
#endif
		return qfalse;
	}

	frontLerp = frac;
	backLerp = 1.0f - frac;

	for ( component = 0 ; component < 3 ; component++ ) {
		tag->origin[component] =
			startBone.matrix[component][3] * backLerp +
			finishBone.matrix[component][3] * frontLerp;
	}

	for ( axis = 0 ; axis < 3 ; axis++ ) {
		for ( component = 0 ; component < 3 ; component++ ) {
			tag->axis[axis][component] =
				startBone.matrix[component][axis] * backLerp +
				finishBone.matrix[component][axis] * frontLerp;
		}
		VectorNormalize( tag->axis[axis] );
	}

#if defined(_XBOX)
	{
		static int s_tagLogBudget = 0;
		if ( s_tagLogBudget < 64 ) {
			XBLF( "STEFX: R_LerpTag MDR ok tag='%s' model='%s' bone=%d frames=%d/%d frac=%g origin=(%g,%g,%g) axis0=(%g,%g,%g)",
				tagName ? tagName : "(null)",
				model->name,
				mdrTag->boneIndex,
				startFrame,
				endFrame,
				frac,
				tag->origin[0],
				tag->origin[1],
				tag->origin[2],
				tag->axis[0][0],
				tag->axis[0][1],
				tag->axis[0][2] );
			s_tagLogBudget++;
		}
	}
#endif

	return qtrue;
}
#endif

/*
================
R_LerpTag
================
*/
void	R_LerpTag( orientation_t *tag, qhandle_t handle, int startFrame, int endFrame, 
					 float frac, const char *tagName ) {
	md3Tag_t	*start, *finish;
	int		i;
	float		frontLerp, backLerp;
	model_t		*model;

	model = R_GetModelByHandle( handle );
	if ( model->md3[0] ) 
	{
		start = R_GetTag( model->md3[0], startFrame, tagName );
		finish = R_GetTag( model->md3[0], endFrame, tagName );
	}
#ifdef STEFX_ELITE_FORCE_SP
	else if ( model->md4 )
	{
		if ( R_STEFX_LerpMDRTag( tag, model, startFrame, endFrame, frac, tagName ) ) {
			return;
		}

		AxisClear( tag->axis );
		VectorClear( tag->origin );
		return;
	}
#endif
	else
	{
		AxisClear( tag->axis );
		VectorClear( tag->origin );
		return;
	}

	if ( !start || !finish ) {
		AxisClear( tag->axis );
		VectorClear( tag->origin );
		return;
	}

	frontLerp = frac;
	backLerp = 1.0 - frac;

	for ( i = 0 ; i < 3 ; i++ ) {
		tag->origin[i] = start->origin[i] * backLerp +  finish->origin[i] * frontLerp;
		tag->axis[0][i] = start->axis[0][i] * backLerp +  finish->axis[0][i] * frontLerp;
		tag->axis[1][i] = start->axis[1][i] * backLerp +  finish->axis[1][i] * frontLerp;
		tag->axis[2][i] = start->axis[2][i] * backLerp +  finish->axis[2][i] * frontLerp;
	}
	VectorNormalize( tag->axis[0] );
	VectorNormalize( tag->axis[1] );
	VectorNormalize( tag->axis[2] );
}


/*
====================
R_ModelBounds
====================
*/
void R_ModelBounds( qhandle_t handle, vec3_t mins, vec3_t maxs ) {
	model_t		*model;

	model = R_GetModelByHandle( handle );

	if ( model->bmodel ) {
		VectorCopy( model->bmodel->bounds[0], mins );
		VectorCopy( model->bmodel->bounds[1], maxs );
		return;
	}

	if ( model->md3[0] ) {
		md3Header_t	*header;
		md3Frame_t	*frame;
		header = model->md3[0];

		frame = (md3Frame_t *)( (byte *)header + header->ofsFrames );

		VectorCopy( frame->bounds[0], mins );
		VectorCopy( frame->bounds[1], maxs );
	}
	else
	{
		VectorClear( mins );
		VectorClear( maxs );
		return;
	}
}


#ifdef _XBOX
void R_ModelFree(void)
{
	if (CachedModels)
	{
		RE_RegisterModels_DeleteAll();
		delete CachedModels;
		CachedModels = NULL;
	}
}
#endif
