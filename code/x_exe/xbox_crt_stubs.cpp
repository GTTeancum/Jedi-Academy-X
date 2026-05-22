// xbox_crt_stubs.cpp
// __ftol2_sse, __ftol2, ___CxxFrameHandler3, _WinMainCRTStartup in xbox_asm_stubs.asm

#ifdef _XBOX
#include <xtl.h>
#include <d3d8perf.h>
#else
#include <windows.h>
#endif

// Plan-B audit (cross-project diff vs OpenJKDF2): _fltused was being defined
// HERE to "fix R6002 floating point not loaded".  That was wrong — by
// providing __fltused ourselves we satisfied the linker external BEFORE
// libc.lib(fpinit.obj) could, so fpinit.obj never linked in.  Our minimal
// asm-stub finit/fldcw replaced the CRT's full FPU setup (control word
// precision, rounding mode, exception masks, _ctrlfp init, _matherr hook).
// On real hardware the difference was harmless; on CXBX-R LLE GPU it caused
// CreateDevice to hang (CXBX-R does a lot of FP math during NV2A init that
// behaves differently against an under-initialized FPU).
// The compiler auto-emits an __fltused external whenever FP code is present,
// which is sufficient to pull fpinit.obj in.  Don't define it ourselves.


#ifdef _XBOX
/*
 * D3DPERF_GetStatistics is a D3D8I (instrumented/debug) PIX function.
 * The retail d3d8-xbox.lib does not provide it, but D3D8Perf.h inlines
 * several functions (e.g. D3DPERF_SetShowFrameRateInterval) that call it.
 * Stub it out: return a pointer to a static zeroed struct so the inline
 * callers can safely read/write fields without crashing.
 */
extern "C" D3DPERF * WINAPI D3DPERF_GetStatistics()
{
    static D3DPERF s_perf;
    static bool    s_init = false;
    if (!s_init) {
        memset(&s_perf, 0, sizeof(s_perf));
        s_init = true;
    }
    return &s_perf;
}
#endif

extern "C" int __cdecl _stricmp(const char* a, const char* b);
extern "C" int __cdecl _strcmpi(const char* a, const char* b)
{
    return _stricmp(a, b);
}

// STL _String_base stubs - no exceptions on Xbox
namespace std {
    struct _String_base {
        void _Xran() const;
        void _Xlen() const;
    };
    void _String_base::_Xran() const { for(;;) {} }
    void _String_base::_Xlen() const { for(;;) {} }
}
