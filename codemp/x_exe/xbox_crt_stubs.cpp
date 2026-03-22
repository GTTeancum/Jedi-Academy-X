// xbox_crt_stubs.cpp
// __ftol2_sse, __ftol2, ___CxxFrameHandler3, _WinMainCRTStartup
// are all in xbox_asm_stubs.asm (exact symbol names required).

// _strcmpi -> _stricmp (exists in libcd.lib)
extern "C" int __cdecl _stricmp(const char* a, const char* b);
extern "C" int __cdecl _strcmpi(const char* a, const char* b)
{
    return _stricmp(a, b);
}
