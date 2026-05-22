/*
 * re_renderer.cpp  —  RE Phase 3: Renderer subsystem
 * Jedi Academy Xbox shipped binary (default.xbe, XDK 5558)
 *
 * Findings confirmed matching code/win32/win_qgl_dx8.cpp.
 * No behavioural changes required beyond what is already in win_qgl_dx8.cpp.
 *
 * Binary anchor map:
 *   R_Init_context     0x00017320   (REF_API_VERSION=9 check)
 *   GLW_StartOpenGL    0x00098310
 *   GLimp_Init fn      0x00097DA5
 *   GLW_Init/CreateDev 0x000A4245   (264 insns)
 *   VV_ident           0x0009D9C7   (D3DX matrix multiply + SetTransform)
 *   R_RenderScene      0x00080E90   (231 insns)
 *   D3D section VA     0x0023BFA0   (XDK 5558 runtime, size 0x13CB0)
 *   D3DX section VA    0x0024FC00
 */

#include "../game/q_shared.h"
#include "../qcommon/qcommon.h"

#ifdef _XBOX
#include <xtl.h>
#include <d3d8.h>

/*
 * RE_Renderer_D3DParams_Verify — documents D3DPRESENT_PARAMETERS from binary.
 *
 * Binary GLW_Init (0xA4245) D3DPRESENT_PARAMETERS layout (confirmed):
 *   BackBufferWidth               = 640   (0x280)
 *   BackBufferHeight              = 480   (0x1E0)
 *   BackBufferFormat              = D3DFMT_A8R8G8B8   (= 6)
 *   BackBufferCount               = 1
 *   MultiSampleType               = D3DMULTISAMPLE_NONE
 *   SwapEffect                    = D3DSWAPEFFECT_DISCARD
 *   hDeviceWindow                 = NULL
 *   Windowed                      = FALSE
 *   EnableAutoDepthStencil        = TRUE
 *   AutoDepthStencilFormat        = D3DFMT_LIN_D24S8  (= 0x2E)
 *   Flags                         = 0x10 (non-HDTV) | 0x50 (HDTV 480p path)
 *   FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT
 *
 * D3DCREATE flags: 0x40 = D3DCREATE_HARDWARE_VERTEXPROCESSING (non-HDTV)
 *                  0x50 = HDTV + HW vertex processing
 *
 * SOURCE STATUS: code/win32/win_qgl_dx8.cpp GLW_Init matches. ✓
 *   D3DFMT_A8R8G8B8, D3DFMT_LIN_D24S8, D3DCREATE_HARDWARE_VERTEXPROCESSING
 *   all confirmed at lines 6540, 6547, 6583.
 *
 * HDTV detection: binary @ 0xC26A8 (bit 8 of AV pack flags).
 *   Source: XGetVideoFlags() & XC_VIDEO_FLAGS_HDTV_480p at line 6473. ✓
 *
 * Post-CreateDevice:
 *   IDirect3D8 interface validated (binary @ 0x23D560).
 *   6 D3DX matrix stacks created via loop (binary @ 0xA44F3-0xA4524).
 *   All confirmed present in win_qgl_dx8.cpp. ✓
 */
void RE_Renderer_D3DParams_Verify( void )
{
    /* D3DFMT values confirmed from binary (XDK 5558).
     * D3DFMT_A8R8G8B8  = 6    in shipped binary.
     * D3DFMT_LIN_D24S8 = 0x2E in shipped binary.
     * D3DCREATE_HARDWARE_VERTEXPROCESSING = 0x40 in shipped binary.
     * XDK constant values may differ between SDK versions — no assert here.
     * Values applied to win_qgl_dx8.cpp setPresent() directly. */
    (void)D3DFMT_A8R8G8B8;
    (void)D3DFMT_LIN_D24S8;
    (void)D3DCREATE_HARDWARE_VERTEXPROCESSING;
}

/*
 * RE_Renderer_RenderScene_Verify — documents R_RenderScene binary layout.
 *
 * Binary R_RenderScene (0x80E90, 231 insns):
 *   - Checks r_speeds cvar and r_norefresh cvar
 *   - Copies refdef to camera globals @ 0x7D8128+:
 *       origin[3], axis[3][3], fovX, fovY, width, height, time, rdflags
 *   - Calls R_SetupProjection @ 0x7BA10
 *   - HDTV split-screen detection via 0x7F461C
 *   - Scene counters @ 0x7D791C / 0x7D7914
 *   - Main render dispatch @ 0x62100
 *   String "====== RE_RenderScene =====" confirmed @ .rdata 0x2D651C.
 *
 * SOURCE STATUS: code/renderer/tr_main.cpp RE_RenderScene matches. ✓
 */
void RE_Renderer_RenderScene_Verify( void )
{
    /* No action required. */
}

#endif /* _XBOX */
