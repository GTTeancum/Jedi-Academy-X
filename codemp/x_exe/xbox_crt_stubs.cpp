// xbox_crt_stubs.cpp
// __ftol2_sse, __ftol2, ___CxxFrameHandler3, _WinMainCRTStartup
// are all in xbox_asm_stubs.asm (exact symbol names required).

#ifdef _XBOX
#include <xtl.h>
#include <d3d8perf.h>
#else
#include <windows.h>
#endif

#ifdef _XBOX
/*
 * Retail d3d8.lib does not provide this D3D8I performance helper, but
 * D3D8Perf.h inlines callers that need a writable statistics struct.
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

// _strcmpi -> _stricmp (exists in libcd.lib)
extern "C" int __cdecl _stricmp(const char* a, const char* b);
extern "C" int __cdecl _strcmpi(const char* a, const char* b)
{
    return _stricmp(a, b);
}
