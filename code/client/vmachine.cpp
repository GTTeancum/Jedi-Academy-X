// vmachine.cpp -- wrapper to fake virtual machine for client

#include "vmachine.h"
#include <stdarg.h>
#ifdef _XBOX
#include "../win32/xb_log.h"
extern "C" volatile unsigned int g_SPXBClTailStage;
extern "C" volatile unsigned int g_SPXBCGameEntryCurrent;
#endif
#pragma warning (disable : 4514)
/*
==============================================================

VIRTUAL MACHINE

==============================================================
*/
int	VM_Call( int callnum, ... )
{
//	assert (cgvm.entryPoint);
	
	if (cgvm.entryPoint)
	{
		int result;
		va_list ap;
		va_start(ap, callnum);

		switch (callnum)
		{
		case CG_INIT:
		{
			const int arg0 = va_arg(ap, int);
#if defined(STEFX_SP_HOSTED_MP)
			const int arg1 = va_arg(ap, int);
#endif
			va_end(ap);
#ifdef _XBOX
#if defined(STEFX_SP_HOSTED_MP)
			XBLF("STEFX_HM_SP: VM_Call CG_INIT entry=%08x message=%d command=%d",
				(unsigned int)cgvm.entryPoint, arg0, arg1);
#else
			XBLF("STEFX: VM_Call CG_INIT entry=%08x arg0=%d", (unsigned int)cgvm.entryPoint, arg0);
#endif
#endif
#if defined(STEFX_SP_HOSTED_MP)
			result = cgvm.entryPoint(callnum, arg0, arg1, 0, 0, 0, 0, 0, 0);
#else
			result = cgvm.entryPoint(callnum, arg0, 0, 0, 0, 0, 0, 0, 0);
#endif
#ifdef _XBOX
			XBLF("STEFX: VM_Call CG_INIT returned %d", result);
#endif
			return result;
		}
		case CG_DRAW_ACTIVE_FRAME:
		{
#ifdef _XBOX
			g_SPXBClTailStage = 0x56443030; /* 'VD00' */
#endif
			const int arg0 = va_arg(ap, int);
			const int arg1 = va_arg(ap, int);
			const int arg2 = va_arg(ap, int);
#ifdef _XBOX
			g_SPXBClTailStage = 0x56443031; /* 'VD01' */
#endif
#ifdef _XBOX
			if (arg0 > 90000)
			{
				XBLF("JA: VM_Call enter DRAW_ACTIVE_FRAME time=%d stereo=%d arg2=%d", arg0, arg1, arg2);
			}
#endif
			va_end(ap);
#ifdef _XBOX
			g_SPXBClTailStage = 0x56443032; /* 'VD02' */
			g_SPXBCGameEntryCurrent = (unsigned int)cgvm.entryPoint;
			g_SPXBClTailStage = 0x56443033; /* 'VD03' */
#endif
			result = cgvm.entryPoint(callnum, arg0, arg1, arg2, 0, 0, 0, 0, 0);
#ifdef _XBOX
			g_SPXBClTailStage = 0x56443034; /* 'VD04' */
			if (arg0 > 90000)
			{
				XBLF("JA: VM_Call return DRAW_ACTIVE_FRAME time=%d result=%d", arg0, result);
			}
#endif
			return result;
		}
		case CG_CAMERA_POS:
		case CG_CAMERA_ANG:
		{
#if defined(STEFX_SP_HOSTED_MP)
			static int s_stefxHostedCameraExportLogged = 0;
			va_end(ap);
			if (!s_stefxHostedCameraExportLogged)
			{
#ifdef _XBOX
				XBLF("STEFX_HM_SP: SP-only cgame camera exports unavailable; using player-state viewpoint");
#endif
				s_stefxHostedCameraExportLogged = 1;
			}
			return 0;
#else
			const int arg0 = va_arg(ap, int);
			va_end(ap);
			return cgvm.entryPoint(callnum, arg0, 0, 0, 0, 0, 0, 0, 0);
#endif
		}
		case CG_SHUTDOWN:
		case CG_CONSOLE_COMMAND:
		case CG_DRAW_DATAPAD_OBJECTIVES:
		case CG_DRAW_DATAPAD_WEAPONS:
		case CG_DRAW_DATAPAD_INVENTORY:
		case CG_DRAW_DATAPAD_FORCEPOWERS:
			va_end(ap);
			return cgvm.entryPoint(callnum, 0, 0, 0, 0, 0, 0, 0, 0);
		default:
			va_end(ap);
			return cgvm.entryPoint(callnum, 0, 0, 0, 0, 0, 0, 0, 0);
		}
	}
	
	return -1;
}

/*
============
VM_DllSyscall

we pass this to the cgame dll to call back into the client
============
*/
extern int CL_CgameSystemCalls( int *args );
extern int CL_UISystemCalls( int *args );

int VM_DllSyscall( int arg, ... ) {
//	return cgvm->systemCall( &arg );
	return CL_CgameSystemCalls( &arg );
}
