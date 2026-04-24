// xbox_crt_stubs.cpp
// __ftol2_sse, __ftol2, ___CxxFrameHandler3, _WinMainCRTStartup in xbox_asm_stubs.asm

#ifdef _XBOX
#include <xtl.h>
#include <d3d8perf.h>
#else
#include <windows.h>
#endif

// Force FPU support to be linked in - fixes R6002 "floating point not loaded"
extern "C" int _fltused = 1;


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
