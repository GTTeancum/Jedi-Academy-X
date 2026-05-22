/*
 * fakegl_dx_extras.h
 *
 * Force-included into gl_fakegl.cpp ONLY (per-file vcproj /FI flag).
 * Companion per-file flag /U_USE_XGMATH undefines _USE_XGMATH so xtl.h
 * does not pull xgmath.h, which would conflict with D3DX8Math.h's
 * XGVECTOR/XGMATRIX struct definitions.
 *
 * Sequence:
 *   1. Define NOD3D so xtl.h (included next) skips its d3d8.h and d3dx8.h
 *      includes — we want our Xbox-specific D3D8-Xbox.h to claim the
 *      _D3D8_H_ include guard, not the PC d3d8.h.
 *   2. Pull xtl.h: gives us excpt/stdarg/windef/winbase/xbox plus all the
 *      Xbox base types we need.
 *   3. Undef NOD3D (so any later #include <d3d8.h> behaves normally — but
 *      it'll be guarded out anyway by step 4).
 *   4. Include D3D8-Xbox.h directly — claims _D3D8_H_, makes IDirect3D
 *      Palette8/PushBuffer8/Fixup8, D3DDevice_Reset/_GetBackBuffer/
 *      _BlockUntilIdle, etc. visible.  When gl_fakegl.cpp later does
 *      #include "xtl.h", the second xtl.h is a no-op (header guarded).
 *   5. Include d3dx8.h directly — gives us the full D3DX function set
 *      (D3DXMatrix*, D3DXCreateMatrixStack, D3DXGetErrorStringA,
 *      D3DXReady, etc.).  Compatible with our setup because xgmath.h
 *      was suppressed via /U_USE_XGMATH in step 0.
 *   6. The Xbox MSAA mode #defines — verbatim from Microsoft's wsdk
 *      d3d8types-xbox.h.  Wrapped in #ifndef so a future install with
 *      the correct local d3d8types-xbox.h takes precedence.
 *
 * Per RENDERER_GRAFT.md: gl_fakegl.cpp itself is byte-identical to the
 * xquake reference; everything in this file is build-system plumbing.
 */

#ifndef _FAKEGL_DX_EXTRAS_H_
#define _FAKEGL_DX_EXTRAS_H_

/* ---- Step 1: skip xtl.h's d3d8.h/d3dx8.h/dsound.h pull-ins -- *
 * FakeGL uses none of these directly via xtl — we provide d3d/d3dx
 * ourselves below.  DirectSound is unused entirely by FakeGL. */
#ifndef NOD3D
#define NOD3D
#define _FAKEGL_DEFINED_NOD3D_
#endif
#ifndef NODSOUND
#define NODSOUND
#define _FAKEGL_DEFINED_NODSOUND_
#endif

/* ---- Step 2: master include for Xbox base types (no d3d/dsound yet) ---- */
#include <xtl.h>

/* ---- Step 3: restore NOD3D / NODSOUND state ---- */
#ifdef _FAKEGL_DEFINED_NOD3D_
#undef NOD3D
#undef _FAKEGL_DEFINED_NOD3D_
#endif
#ifdef _FAKEGL_DEFINED_NODSOUND_
#undef NODSOUND
#undef _FAKEGL_DEFINED_NODSOUND_
#endif

/* ---- Step 3.5: COM + GDI macros that D3DX8Core/Math/Tex/Shape rely on.
 * Normally pulled in via d3d8.h's chain — suppressed by NOD3D, so we
 * include directly. objbase.h: STDMETHOD/IUnknown/THIS/PURE.
 * wingdi.h: LOGFONT/PALETTEENTRY/LPGLYPHMETRICSFLOAT. */
#include <objbase.h>
#include <wingdi.h>

/* ---- Step 4: claim _D3D8_H_ for the Xbox D3D header ---- */
#include <D3D8-Xbox.h>

/* ---- Step 5: full D3DX8 (now safe — _USE_XGMATH was undefined).
 * Use absolute XDK path to bypass the local code/win32/d3dx8.h shim
 * (which is a minimal subset; the rest of the codebase relies on it,
 * but FakeGL needs the real, full D3DX8 surface). */
#include "C:/XDK/xbox/include/d3dx8.h"

/* ---- Step 6: Xbox MSAA extension modes (additive #defines) ---- */
#ifndef D3DMULTISAMPLE_2_SAMPLES_MULTISAMPLE_LINEAR
#define D3DMULTISAMPLE_2_SAMPLES_MULTISAMPLE_LINEAR              0x1021
#define D3DMULTISAMPLE_2_SAMPLES_MULTISAMPLE_QUINCUNX            0x1121
#define D3DMULTISAMPLE_2_SAMPLES_SUPERSAMPLE_HORIZONTAL_LINEAR   0x2021
#define D3DMULTISAMPLE_2_SAMPLES_SUPERSAMPLE_VERTICAL_LINEAR     0x2012

#define D3DMULTISAMPLE_4_SAMPLES_MULTISAMPLE_LINEAR              0x1022
#define D3DMULTISAMPLE_4_SAMPLES_MULTISAMPLE_GAUSSIAN            0x1222
#define D3DMULTISAMPLE_4_SAMPLES_SUPERSAMPLE_LINEAR              0x2022
#define D3DMULTISAMPLE_4_SAMPLES_SUPERSAMPLE_GAUSSIAN            0x2222

#define D3DMULTISAMPLE_9_SAMPLES_MULTISAMPLE_GAUSSIAN            0x1233
#define D3DMULTISAMPLE_9_SAMPLES_SUPERSAMPLE_GAUSSIAN            0x2233

#define D3DMULTISAMPLE_PREFILTER_FORMAT_DEFAULT                  0x00000
#define D3DMULTISAMPLE_PREFILTER_FORMAT_X1R5G5B5                 0x10000
#define D3DMULTISAMPLE_PREFILTER_FORMAT_R5G6B5                   0x20000
#define D3DMULTISAMPLE_PREFILTER_FORMAT_X8R8G8B8                 0x30000
#define D3DMULTISAMPLE_PREFILTER_FORMAT_A8R8G8B8                 0x40000
#endif

#endif /* _FAKEGL_DX_EXTRAS_H_ */
