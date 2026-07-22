#include "../qcommon/exe_headers.h"

#include "../qcommon/miniheap.h"
#include "../qcommon/timing.h"
#include "../renderer/tr_local.h"
#include "../cgame/cg_local.h"
#include "../renderer/modelmem.h"
#include "xb_log_mp_compat.h"

/*
 * The Holomatch executable compiles the SP renderer byte-for-byte.  These are
 * executable-side owners for state that lives in SP client/server modules, plus
 * dead inherited renderer entry points that Elite Force assets never use.
 */

#define G2_VERT_SPACE_SERVER_SIZE 256

CMiniHeap *G2VertSpaceServer = NULL;
CMiniHeap CMiniHeap_singleton(G2_VERT_SPACE_SERVER_SIZE * 1024);

ModelMemoryManager ModelMem;

qboolean vidRestartReloadMap = qfalse;
qboolean gbAlreadyDoingLoad = qfalse;

char entityVisList[MAX_GENTITIES + 1000 + 256];
int zfFaceShaders[3] = { -1, -1, -1 };
int tfTorsoShader = -1;

timing_c G2PerformanceTimer_PreciseFrame;
int G2Time_PreciseFrame = 0;

class STEFXNullModelInfoArray : public IGhoul2InfoArray
{
public:
	virtual int New()
	{
		return 1;
	}

	virtual void Delete(int handle)
	{
		(void)handle;
		m_empty.clear();
	}

	virtual bool IsValid(int handle) const
	{
		return handle == 0 || handle == 1;
	}

	virtual vector<CGhoul2Info> &Get(int handle)
	{
		(void)handle;
		return m_empty;
	}

	virtual const vector<CGhoul2Info> &Get(int handle) const
	{
		(void)handle;
		return m_empty;
	}

private:
	vector<CGhoul2Info> m_empty;
};

IGhoul2InfoArray &TheGhoul2InfoArray(void)
{
	static STEFXNullModelInfoArray emptyArray;
	return emptyArray;
}

void Ghoul2InfoArray_Free(void)
{
}

const char *Sys_RemapPath(const char *filename)
{
	if (filename && !Q_stricmpn(filename, "BaseEF\\", 7))
	{
		return va("D:\\BaseEF\\%s", filename + 7);
	}
	if (filename && !Q_stricmpn(filename, "base\\", 5))
	{
		return va("D:\\BaseEF\\%s", filename + 5);
	}
	return va("D:\\%s", filename ? filename : "");
}

extern "C" void XBLog_MainProbe(void)
{
	XBLog_PreCRTProbe();
}

extern "C" void XBLog_StartupProbe(const char *msg)
{
	XBLog_WriteRingMarker(msg);
}

extern "C" void XBLog_Phase(const char *msg)
{
	XBLog_WriteRingMarker(msg);
}

bool Sys_IsDirectMapBoot(void)
{
	return true;
}

int Menus_AnyFullScreenVisible(void)
{
	return 0;
}

void Swap_Init(void)
{
}

void CM_CleanLeafCache(void)
{
}

qboolean FS_STEFX_IsHeapFileBuffer(const void *buffer)
{
	(void)buffer;
	return qfalse;
}

qboolean FS_STEFX_FreeHeapFileBuffer(void *buffer)
{
	(void)buffer;
	return qfalse;
}

void G2Time_ResetTimers(void)
{
	G2Time_PreciseFrame = 0;
}

void G2Time_ReportTimers(void)
{
}

void ClearTheBonePool(void)
{
}

void *BonePoolTempAlloc(unsigned long size)
{
	(void)size;
	return NULL;
}

void BonePoolTempFree(void *buffer)
{
	(void)buffer;
}

bool IsBonePoolPointer(void *buffer)
{
	(void)buffer;
	return false;
}

qboolean R_InitializeWireframeAutomap(void)
{
	return qfalse;
}

void R_AutomapElevationAdjustment(float newHeight)
{
	(void)newHeight;
}

void R_DestroyWireframeMap(void)
{
}

void R_SVModelInit(void)
{
}

qhandle_t RE_RegisterServerSkin(const char *name)
{
	return RE_RegisterSkin(name);
}

void RE_RegisterMedia_LevelLoadBegin(const char *mapName, ForceReload_e forceReload)
{
	RE_RegisterMedia_LevelLoadBegin(mapName, forceReload, qfalse);
}

qboolean R_LoadMDXM(model_t *model, void *buffer, const char *name, qboolean &alreadyCached)
{
	(void)model;
	(void)buffer;
	(void)name;
	alreadyCached = qfalse;
	return qfalse;
}

qboolean R_LoadMDXA(model_t *model, void *buffer, const char *name, qboolean &alreadyCached)
{
	(void)model;
	(void)buffer;
	(void)name;
	alreadyCached = qfalse;
	return qfalse;
}

void R_AddGhoulSurfaces(trRefEntity_t *entity)
{
	(void)entity;
}

void RB_SurfaceGhoul(CRenderableSurface *surface)
{
	(void)surface;
}
