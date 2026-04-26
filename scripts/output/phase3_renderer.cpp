/*
 * phase3_renderer.cpp
 * RE Phase 3 — Renderer subsystem reconstruction
 * Source: default.xbe (Jedi Academy Xbox, XDK 5558)
 *
 * Functions reconstructed from binary analysis (phase3_renderer.txt).
 * Verified against code/win32/win_qgl_dx8.cpp — all D3D parameters MATCH.
 * No source patches required.
 *
 * Key binary addresses:
 *   R_Init_context     0x17320   (wrapper; calls 0x75FA0 with REF_API_VERSION=9)
 *   GLW_StartOpenGL    0x98310   (QGL driver load; calls GLimp_Init)
 *   GLimp_Init fn      0x97DA5   (validates GL driver function ptrs)
 *   GLW_Init/CreateDev 0xA4245   (D3D device creation; 264 insns)
 *   VV_ident           0x9D9C7   (D3DX matrix multiply + SetTransform)
 *   R_RenderScene      0x80E90   (view setup + render dispatch; 231 insns)
 *   D3D section        0x23BFA0  (XDK 5558 D3D runtime, size 0x13CB0)
 *   D3DX section       0x24FC00  (D3DX library)
 */

#ifdef RE_PHASE3_RENDERER_COMPILE

#include "../game/q_shared.h"
#include "../qcommon/qcommon.h"
#include "../renderer/qgl_console.h"
#include <xtl.h>
#include <d3d8.h>

/* Renderer global (stored at [0x81C8A4] in binary).
   Allocated as 0x508C bytes via Z_Malloc inside GLW_Init. */
extern void *glw_state_re;  /* glwstate_t* in source */

/* D3D section base (IDirect3D8 object pointer stored by XDK runtime).
   CreateDevice entry point resolved from D3D section @ 0x23F570. */

/* =========================================================================
 * R_Init_context  (binary @ 0x17320, 26 insns)
 *
 * Called from Com_Init (binary @ 0x237F6).
 * Passes REF_API_VERSION = 9 to the ref init function.
 * On failure prints "Couldn't initialize refresh" and calls Com_Error.
 * On success copies 0x2C (11) DWORDs of ref export table to 0x47E168.
 * Then registers "cl_paused" cvar.
 * ========================================================================= */
static void *R_Init_context_RE( void )
{
    Com_Printf( "----- Initializing Renderer ----\n" );

    /* 0x75FA0: validates REF_API_VERSION==9, clears 0xB4-byte renderer
       table @ 0x7D3378, returns refexport_t* or NULL on version mismatch. */
    void *re = R_Init( 9 /* REF_API_VERSION */ );

    Com_Printf( "-------------------------------\n" );

    if ( !re ) {
        Com_Error( ERR_DROP, "Couldn't initialize refresh" );
    }

    /* Copy ref export table (0x2C DWORDs) to client local @ 0x47E168 */
    /* memcpy( cl_re_exports, re, 0x2C * 4 ); */

    Cvar_Get( "cl_paused", "0", 0 );
    return re;
}

/* =========================================================================
 * GLW_Init / CreateDevice  (binary @ 0xA4245, 264 insns)
 *
 * D3DPRESENT_PARAMETERS as observed in binary (confirmed match to source):
 *
 *   BackBufferWidth               = 640   (0x280)
 *   BackBufferHeight              = 480   (0x1E0)
 *   BackBufferFormat              = D3DFMT_A8R8G8B8   (=6)
 *   BackBufferCount               = 1
 *   MultiSampleType               = D3DMULTISAMPLE_NONE (=0)
 *   SwapEffect                    = D3DSWAPEFFECT_DISCARD (=1 on Xbox)
 *   hDeviceWindow                 = NULL
 *   Windowed                      = FALSE
 *   EnableAutoDepthStencil        = TRUE
 *   AutoDepthStencilFormat        = D3DFMT_LIN_D24S8 (=0x2E)
 *   Flags                         = 0x10 (non-HDTV) OR 0x50 (HDTV path)
 *   FullScreen_RefreshRateInHz    = 0
 *   FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT (=0)
 *
 * D3DCREATE flags:
 *   0x40 = D3DCREATE_HARDWARE_VERTEXPROCESSING  (non-HDTV)
 *   0x50 = HDTV + HW vertex processing
 *
 * HDTV detection: binary calls 0xC26A8; if bit 8 set -> HDTV path.
 *
 * Post-CreateDevice:
 *   - IDirect3D8 interface validated via 0x23D560
 *   - D3DX matrix stacks created (loop 6 iterations) via 0x252DC6
 *   - SetRenderState / viewport init via 0x980C0
 *   - Renderer globals pointer stored at [0x81C8A4]
 * ========================================================================= */
static void GLW_Init_RE( void )
{
    D3DPRESENT_PARAMETERS present;
    ZeroMemory( &present, sizeof(present) );

    present.BackBufferWidth               = 640;
    present.BackBufferHeight              = 480;
    present.BackBufferFormat              = D3DFMT_A8R8G8B8;
    present.BackBufferCount               = 1;
    present.MultiSampleType               = D3DMULTISAMPLE_NONE;
    present.SwapEffect                    = D3DSWAPEFFECT_DISCARD;
    present.hDeviceWindow                 = NULL;
    present.Windowed                      = FALSE;
    present.EnableAutoDepthStencil        = TRUE;
    present.AutoDepthStencilFormat        = D3DFMT_LIN_D24S8;
    present.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;

    /* HDTV detection (binary @ 0xC26A8 -> checks AV pack type) */
    BOOL bHDTV = FALSE; /* IsHDTVCapable() */
    DWORD dwCreateFlags = D3DCREATE_HARDWARE_VERTEXPROCESSING; /* 0x40 */
    present.Flags = 0x10;
    if ( bHDTV ) {
        present.Flags = 0x50;
        dwCreateFlags = 0x50; /* HDTV + HW vertex */
    }

    /* Allocate renderer struct: 0x508C bytes via Z_Malloc (hunk TAG_RENDERER) */
    /* glw_state_re = Z_Malloc(0x508C, TAG_RENDERER, qtrue); */

    IDirect3DDevice8 *pDevice = NULL;
    /* IDirect3D8::CreateDevice called via D3D section thunk @ 0x23F570 */
    HRESULT hr = IDirect3D8::CreateDevice(
        D3DADAPTER_DEFAULT,
        D3DDEVTYPE_HAL,
        NULL,          /* hFocusWindow — NULL on Xbox */
        dwCreateFlags,
        &present,
        &pDevice );

    if ( FAILED(hr) ) {
        Com_Printf( "Failed to create device. That's bad.\n" );
    }

    /* Validate IDirect3D8 interface (binary @ 0x23D560) */
    /* g_d3dValid = (IDirect3D8::CheckDeviceFormat(...) == S_OK); */

    /* Create 6 D3DX matrix stacks (binary loop @ 0xA44F3-0xA4524).
       Each stack created via D3DXCreateMatrixStack @ 0x252DC6,
       then GetTop() called via vtable [edx+0x14] to prime the stack. */
    for ( int i = 0; i < 6; ++i ) {
        /* D3DXCreateMatrixStack(0, &glw_state_re->matrixStacks[i]); */
        /* glw_state_re->matrixStacks[i]->GetTop(); */
    }
}

/* =========================================================================
 * VV_ident  (binary @ 0x9D9C7)
 *
 * Per-frame identity/composite matrix upload.
 * Calls D3DX matrix multiply @ 0x2518BA, then IDirect3DDevice8::SetTransform
 * via vtable offset [edx + 0x1C].
 *
 * SOURCE: matches R_SetIdentityMatrix / R_RotateForViewer pattern in
 *         code/win32/win_qgl_dx8.cpp.
 * ========================================================================= */
static void VV_ident_RE( D3DTRANSFORMSTATETYPE state, const D3DMATRIX *pMat )
{
    D3DMATRIX result;
    /* D3DXMatrixMultiply(&result, pMat, &g_viewMatrix); — 0x2518BA */
    /* device->SetTransform(state, &result); — vtable [edx+0x1C]      */
    (void)state; (void)pMat; /* suppress unused warnings in RE stub */
}

/* =========================================================================
 * R_RenderScene  (binary @ 0x80E90, 231 insns)
 *
 * Called from CL_RenderFrame.
 * Flow:
 *   1. Checks r_speeds cvar and r_norefresh cvar
 *   2. Copies refdef fields to camera globals @ 0x7D8128+:
 *        origin[3], axis[3][3], fovX, fovY, width, height, time, rdflags
 *   3. Calls R_SetupProjection @ 0x7BA10
 *   4. HDTV split-screen detection via 0x7F461C
 *   5. Scene counters at 0x7D791C / 0x7D7914
 *   6. Main render dispatch @ 0x62100
 *
 * SOURCE: matches RE_RenderScene in code/renderer/tr_main.cpp exactly.
 *   String "====== RE_RenderScene =====" confirmed at .rdata 0x2D651C.
 * ========================================================================= */
static void R_RenderScene_RE( const refdef_t *fd )
{
    if ( !tr.registered ) {
        Com_Printf( "====== RE_RenderScene =====\n" );
        return;
    }
    /* ... standard Q3/JA RenderScene body ... */
    (void)fd;
}

#endif /* RE_PHASE3_RENDERER_COMPILE */
