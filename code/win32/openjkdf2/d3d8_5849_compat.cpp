/*
 * d3d8_5849_compat.cpp — Plan-B (OpenJKDF2 1:1) D3D8 API shim
 *
 * JKA's renderer was authored against XDK 5849 d3d8.lib which exposes:
 *   _D3DDevice_BeginPush@8                  : 2 args (count, ppPush)
 *   _D3DDevice_SetVertexShaderConstant@12   : 3 args (reg, data, count)
 *
 * OpenJKDF2's chosen 5558 d3d8.lib only has the split-overload variants:
 *   _D3DDevice_BeginPush@4                  : 1 arg, returns DWORD*
 *   _D3DDevice_SetVertexShaderConstant1@8 / 4@8 / NotInline@12
 *
 * The stdcall name decorations (@8 vs @4) make the 5849 and 5558 symbols
 * distinct at link time even though the unmangled name is identical, so
 * we can provide our own @8 wrapper without colliding.
 *
 * These calls are only made on JKA codepaths post-license-screen
 * (tr_WorldEffects = weather/clouds; win_stencilshadow = saber/lightning
 * shadow volumes).  Implementations here are functional enough to keep
 * JKA from crashing if those paths fire before they're properly wired.
 */

#include <xtl.h>
#include <stdlib.h>

extern "C" {

/* Scratch push-buffer that JKA writes into and then ignores after we
 * return.  Sized large enough for typical tr_WorldEffects batches. */
#define COMPAT_PUSH_SCRATCH_DWORDS 4096
static DWORD g_compatPushScratch[COMPAT_PUSH_SCRATCH_DWORDS];

/* 2-arg BeginPush (5849 signature).  Symbol exports as
 * _D3DDevice_BeginPush@8 — distinct from 5558's _D3DDevice_BeginPush@4
 * so no link collision. */
void WINAPI D3DDevice_BeginPush(DWORD count, DWORD **ppPush)
{
    /* Compat impl: hand back a scratch heap region.  Writes JKA does to
     * the returned pointer go to RAM, not GPU push buffer, so the actual
     * draw is silently dropped — fine for non-critical post-license paths. */
    if (ppPush) {
        if (count > COMPAT_PUSH_SCRATCH_DWORDS) count = COMPAT_PUSH_SCRATCH_DWORDS;
        *ppPush = g_compatPushScratch;
    }
}

/* 3-arg SetVertexShaderConstant (5849 signature).  Forward to whichever
 * 5558 split overload matches count.  D3DFASTCALL = __fastcall, but
 * symbol decoration matches because no @suffix is added for __fastcall.
 * The 5558 headers declare these as D3DFASTCALL; calling them from our
 * stdcall shim works because they're inline-defined in d3d8.h. */
void WINAPI D3DDevice_SetVertexShaderConstant(INT reg, const void *pData, DWORD count)
{
    extern void D3DFASTCALL D3DDevice_SetVertexShaderConstant1(INT, const void*);
    extern void D3DFASTCALL D3DDevice_SetVertexShaderConstant4(INT, const void*);
    extern void D3DFASTCALL D3DDevice_SetVertexShaderConstantNotInline(INT, const void*, DWORD);

    if (count == 1)      D3DDevice_SetVertexShaderConstant1(reg, pData);
    else if (count == 4) D3DDevice_SetVertexShaderConstant4(reg, pData);
    else                 D3DDevice_SetVertexShaderConstantNotInline(reg, pData, count);
}

} /* extern "C" */
