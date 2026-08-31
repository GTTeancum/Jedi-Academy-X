#ifndef XB_PERF_H
#define XB_PERF_H

#if defined(_XBOX)

extern "C" volatile unsigned int g_SPXBPerfRenderTotalMsec;
extern "C" volatile unsigned int g_SPXBPerfRenderSetupMsec;
extern "C" volatile unsigned int g_SPXBPerfRenderMarkLeavesMsec;
extern "C" volatile unsigned int g_SPXBPerfRenderWorldMsec;
extern "C" volatile unsigned int g_SPXBPerfRenderPolysMsec;
extern "C" volatile unsigned int g_SPXBPerfRenderProjectionMsec;
extern "C" volatile unsigned int g_SPXBPerfRenderEntitiesMsec;
extern "C" volatile unsigned int g_SPXBPerfRenderSortMsec;
extern "C" volatile unsigned int g_SPXBPerfRenderDebugMsec;
extern "C" volatile unsigned int g_SPXBPerfRenderViews;
extern "C" volatile unsigned int g_SPXBPerfRenderPortals;
extern "C" volatile unsigned int g_SPXBPerfRenderDrawSurfs;
extern "C" volatile unsigned int g_SPXBPerfRenderRefEntities;
extern "C" volatile unsigned int g_SPXBPerfRenderLeafs;
extern "C" volatile unsigned int g_SPXBPerfEntityModelSetupCycles;
extern "C" volatile unsigned int g_SPXBPerfEntityModelSetupCalls;
extern "C" volatile unsigned int g_SPXBPerfEntityMeshCycles;
extern "C" volatile unsigned int g_SPXBPerfEntityMeshCalls;
extern "C" volatile unsigned int g_SPXBPerfEntityBrushCycles;
extern "C" volatile unsigned int g_SPXBPerfEntityBrushCalls;
extern "C" volatile unsigned int g_SPXBPerfEntityAnimCycles;
extern "C" volatile unsigned int g_SPXBPerfEntityAnimCalls;
extern "C" volatile unsigned int g_SPXBPerfEntitySimpleCycles;
extern "C" volatile unsigned int g_SPXBPerfEntitySimpleCalls;
extern "C" volatile unsigned int g_SPXBPerfBackendSurfaces;
extern "C" volatile unsigned int g_SPXBPerfBackendVertexes;
extern "C" volatile unsigned int g_SPXBPerfBackendIndexes;
extern "C" volatile unsigned int g_SPXBPerfBackendTotalIndexes;
extern "C" volatile unsigned int g_SPXBPerfFinishMsec;
extern "C" volatile unsigned int g_SPXBPerfPresentMsec;
extern "C" volatile unsigned int g_SPXBPerfBackendBatches;
extern "C" volatile unsigned int g_SPXBPerfSubmitCallsCurrent;
extern "C" volatile unsigned int g_SPXBPerfDrawCyclesCurrent;
extern "C" volatile unsigned int g_SPXBPerfDrawStateCyclesCurrent;
extern "C" volatile unsigned int g_SPXBPerfDrawReserveCyclesCurrent;
extern "C" volatile unsigned int g_SPXBPerfDrawSetStreamCyclesCurrent;
extern "C" volatile unsigned int g_SPXBPerfDrawBeginPushCyclesCurrent;
extern "C" volatile unsigned int g_SPXBPerfDrawPointerCyclesCurrent;
extern "C" volatile unsigned int g_SPXBPerfDrawBeginPushMaxCyclesCurrent;
extern "C" volatile unsigned int g_SPXBPerfDrawBeginPushMaxDwordsCurrent;
extern "C" volatile unsigned int g_SPXBPerfDrawBeginPushOver100KCurrent;
extern "C" volatile unsigned int g_SPXBPerfDrawBeginPushOver1MsecCurrent;
extern "C" volatile unsigned int g_SPXBPerfDrawBeginPushOver10MsecCurrent;
extern "C" volatile unsigned int g_SPXBPerfDrawBeginPushMaxStateCurrent;
extern "C" volatile unsigned int g_SPXBPerfIndexedOpaqueCallsCurrent;
extern "C" volatile unsigned int g_SPXBPerfIndexedBlendCallsCurrent;
extern "C" volatile unsigned int g_SPXBPerfIndexedAlphaTestCallsCurrent;
extern "C" volatile unsigned int g_SPXBPerfIndexedNoDepthWriteCallsCurrent;
extern "C" volatile unsigned int g_SPXBPerfIndexedNoDepthTestCallsCurrent;
extern "C" volatile unsigned int g_SPXBPerfIndexedTwoSidedCallsCurrent;
extern "C" volatile unsigned int g_SPXBPerfIndexedBlendIndexesCurrent;
extern "C" volatile unsigned int g_SPXBPerfIndexedAlphaTestIndexesCurrent;
extern "C" volatile unsigned int g_SPXBPerfIndexedNoDepthWriteIndexesCurrent;
extern "C" volatile unsigned int g_SPXBPerfIndexedTwoSidedIndexesCurrent;
extern "C" volatile unsigned int g_SPXBPerfDrawPackCyclesCurrent;
extern "C" volatile unsigned int g_SPXBPerfDrawIndexCyclesCurrent;
extern "C" volatile unsigned int g_SPXBPerfDrawSubmitCyclesCurrent;
extern "C" volatile unsigned int g_SPXBPerfBackendDrawSurfsMsec;
extern "C" volatile unsigned int g_SPXBPerfBackendSwapMsec;
extern "C" volatile unsigned int g_SPXBPerfBackendOtherMsec;
extern "C" volatile unsigned int g_SPXBPerfIndexedSubmitCallsCurrent;
extern "C" volatile unsigned int g_SPXBPerfImmediateSubmitCallsCurrent;
extern "C" volatile unsigned int g_SPXBPerfIndexedTex1CallsCurrent;
extern "C" volatile unsigned int g_SPXBPerfIndexedReserveDwordsCurrent;
extern "C" volatile unsigned int g_SPXBPerfImmediateReserveDwordsCurrent;
extern "C" volatile unsigned int g_SPXBPerfSampleActive;
extern "C" volatile unsigned int g_SPXBPerfSampleSerial;
extern "C" volatile unsigned int g_SPXBPerfReuseCandidatesCurrent;
extern "C" volatile unsigned int g_SPXBPerfReuseCandidateDwordsCurrent;
extern "C" volatile unsigned int g_SPXBPerfReuseUniqueCurrent;
extern "C" volatile unsigned int g_SPXBPerfReuseCrossViewHitsCurrent;
extern "C" volatile unsigned int g_SPXBPerfReuseCrossViewDwordsCurrent;
extern "C" volatile unsigned int g_SPXBPerfReuseTableFullCurrent;
extern "C" volatile unsigned int g_SPXBPerfReuseHashCyclesCurrent;

static __forceinline unsigned __int64 STEFX_XboxReadTsc( void )
{
	unsigned __int64 result;
	__asm
	{
		rdtsc
		mov dword ptr [result], eax
		mov dword ptr [result + 4], edx
	}
	return result;
}

static __forceinline unsigned int STEFX_XboxElapsedCycles( unsigned __int64 start )
{
	return (unsigned int)(STEFX_XboxReadTsc() - start);
}

static __forceinline unsigned int STEFX_XboxCyclesToMsec( unsigned __int64 cycles )
{
	return (unsigned int)((cycles + 366666u) / 733333u);
}

#endif

#endif
