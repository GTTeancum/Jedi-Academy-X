// vmachine.cpp -- wrapper to fake virtual machine for client

#include "vmachine.h"
#include <stdarg.h>
#ifdef _XBOX
#include "../win32/xb_log.h"
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
			va_end(ap);
			return cgvm.entryPoint(callnum, arg0, 0, 0, 0, 0, 0, 0, 0);
		}
		case CG_DRAW_ACTIVE_FRAME:
		{
			const int arg0 = va_arg(ap, int);
			const int arg1 = va_arg(ap, int);
			const int arg2 = va_arg(ap, int);
#ifdef _XBOX
			if (arg0 > 90000)
			{
				XBLF("JA: VM_Call enter DRAW_ACTIVE_FRAME time=%d stereo=%d arg2=%d", arg0, arg1, arg2);
			}
#endif
			va_end(ap);
			result = cgvm.entryPoint(callnum, arg0, arg1, arg2, 0, 0, 0, 0, 0);
#ifdef _XBOX
			if (arg0 > 90000)
			{
				XBLF("JA: VM_Call return DRAW_ACTIVE_FRAME time=%d result=%d", arg0, result);
			}
#endif
			return result;
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
