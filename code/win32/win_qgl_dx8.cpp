/*
 * win_qgl_dx8.cpp — Plan-B (OpenJKDF2 1:1) slim adapter
 *
 * Owns nothing GL-related; just lifecycle plumbing.  The qgl_* function-
 * pointer table is GONE — JKA's renderer calls gl_* directly, linked to
 * OpenJKDF2's fakeglx.cpp (in code/win32/openjkdf2/fakeglx.cpp).  This
 * file's job in the new architecture:
 *
 *   1. Allocate / zero glw_state (storage for the renderer's viewport etc.)
 *   2. Drive wglCreateContext / wglMakeCurrent / FakeSwapBuffers wrappers
 *   3. Adopt OpenJKDF2's std3D_Startup initial GL state setup
 *      (glViewport, glMatrixMode, glOrtho, glLoadIdentity, glDisable for
 *       depth/cull/blend/etc.) so the first real frame draws against a
 *      known-good baseline
 *   4. Provide no-op QGL_Init / QGL_Shutdown / QGL_EnableLogging stubs so
 *      win_glimp.cpp's existing call sites still link
 *   5. Stub render-pass helpers (renderObject_*, CreateVertexShader/Pixel)
 *      that the win32 layer historically provided
 *
 * Backup of the previous Plan-A version (with qgl_* table and bridges)
 * lives at win_qgl_dx8.cpp.preB.bak.
 */

#define APIENTRY __stdcall
#define WINAPI   __stdcall

#include "../server/exe_headers.h"

#include <float.h>
#include "../renderer/tr_local.h"
#include "glw_win_dx8.h"
#include "win_local.h"

#ifdef _XBOX
#include <xtl.h>
#include <xb_log.h>
#endif

/* StaticTextureAllocator is referenced from xbox_texture_man; we own the
 * single instance.  Symbol kept here for ABI continuity with the rest of
 * the win32 layer. */
#include "xbox_texture_man.h"
StaticTextureAllocator gTextures;

#ifdef _XBOX
extern "C" void* JkaStaticTextureAlloc(unsigned long size, GLuint texNum)
{
    return gTextures.Allocate(size, texNum);
}
#endif

extern void Z_SetNewDeleteTemporary(bool);

/* fakeglx.cpp lifecycle entry points.  fakeglx.cpp's file-scope wrappers
 * export as C linkage (_FakeSwapBuffers etc. per dumpbin), so our externs
 * here must be extern "C" to match. */
extern "C" {
    HGLRC wglCreateContext(void);
    BOOL  wglMakeCurrent(void);
    BOOL  wglDeleteContext(void);
    void  FakeSwapBuffers(void);
}

/* Plan-B fix: glwstate_t storage is defined in win_glimp_console.cpp:54
 * as `glwstate_t *glw_state = NULL`.  Previously this file also defined
 * it which produced LNK4006 duplicate-definition warnings under
 * /FORCE:MULTIPLE — linker silently kept win_glimp_console's NULL
 * version and discarded our pointer-to-static.  Result: GLW_Init's
 * glw_state->X assignments were writing to a different (NULL) pointer
 * than tr_backend/etc. read from, masking all our state setup.
 *
 * Define the static storage here, but make win_glimp_console's
 * `glw_state` pointer point to it via a one-shot init in GLW_Init. */
static glwstate_t s_glwState;
extern glwstate_t *glw_state;  /* defined in win_glimp_console.cpp */

/* =========================================================================
 *  JKA-specific render-pass stubs (declared in glw_win_dx8.h).
 *  These were win32-layer helpers JKA's renderer no longer calls under
 *  Plan-B (fakeglx draws directly).  Kept as empty bodies so any
 *  straggling external references still link.
 * ========================================================================= */
void renderObject_HACK(void) {}
void renderObject_Light(int /*numIndexes*/, const unsigned short* /*indexes*/) {}
void renderObject_Shadow(int /*primType*/, int /*numIndexes*/, const unsigned short* /*indexes*/) {}
void renderObject_Env(void) {}
void renderObject_Bump(void) {}

bool CreateVertexShader(const CHAR* /*strFilename*/, const DWORD* /*pdwVertexDecl*/, DWORD* pdwVertexShader)
{
    if (pdwVertexShader) *pdwVertexShader = 0;
    return false;
}
bool CreatePixelShader(const CHAR* /*strFilename*/, DWORD* pdwPixelShader)
{
    if (pdwPixelShader) *pdwPixelShader = 0;
    return false;
}

/* Engine-side stragglers from the original win32 layer.  Not touched in
 * Plan-B; provided so the engine still links. */
bool connectSwapOverride = false;
bool bHadPersistedSurface = false;

void SaveCompressedScreenshot( void ) { /* no fakegl equivalent */ }
int  LoadCompressedScreenshot( const char * /*filename*/ ) { return 0; }

/* =========================================================================
 *  QGL_Init / QGL_Shutdown / QGL_EnableLogging
 *  ---
 *  Pre-Plan-B these allocated and populated a 350-entry qgl_* function-
 *  pointer table.  Under Plan-B the engine calls gl_* directly via the
 *  qgl_console.h → fakeglx.h chain — no pointer table to populate.
 *  These stubs exist purely so win_glimp.cpp's existing call sites still
 *  link.
 * ========================================================================= */
qboolean QGL_Init( const char * /*dllname*/ )
{
#ifdef _XBOX
    XBLog_Write("QGL_Init: no-op under Plan-B (gl_* called directly via fakeglx)\n");
#endif
    return qtrue;
}

void QGL_Shutdown( void )
{
}

void QGL_EnableLogging( qboolean /*enable*/ )
{
}

/* =========================================================================
 *  GLW_Init / GLW_Shutdown — drive FakeGL lifecycle.
 *
 *  Per OpenJKDF2's std3D_Startup pattern (src/Platform/Xbox/std3D.c):
 *    1. wglCreateContext(NULL) → fakegl creates D3D8 device internally
 *    2. wglMakeCurrent(NULL, ctx) → set gFakeGL global
 *    3. Explicit initial GL state setup (glViewport, glMatrixMode, glOrtho,
 *       glDisable for depth/cull/blend/texture/alpha)
 *    4. 60-frame stability loop: glClear + FakeSwapBuffers
 *       (gives D3D8 device time to settle before the engine's first draw)
 *
 *  Step 4 is the missing piece in our previous attempts — without it,
 *  the very first engine draw catches D3D8 in an inconsistent state.
 * ========================================================================= */

static HGLRC s_fakeglContext = NULL;

void GLW_Init( int width, int height, int /*colorbits*/, qboolean /*cdsFullscreen*/ )
{
#ifdef _XBOX
    XBLog_Write("GLW_Init: starting (Plan-B / OpenJKDF2 1:1)\n");
#endif

    /* Plan-B fix: point win_glimp_console.cpp's `glw_state` (which it
     * defaults to NULL) at our owned static storage.  Without this, the
     * 150 glw_state->X uses elsewhere in the renderer would deref NULL. */
    if (!glw_state) {
        glw_state = &s_glwState;
#ifdef _XBOX
        XBLog_Write("GLW_Init: glw_state pointer set to s_glwState\n");
#endif
    }

    /* glConfig fields the engine reads. */
    glConfig.vidWidth      = width;
    glConfig.vidHeight     = height;
    glConfig.colorBits     = 32;
    glConfig.depthBits     = 24;
    glConfig.stencilBits   = 8;
    glConfig.deviceSupportsGamma = qfalse;

    /* glw_state defaults — only touch scalar fields, never raw-memset
     * (textureXlat is a std::map). */
    glw_state->isWidescreen   = false;
    glw_state->viewport.X      = 0;
    glw_state->viewport.Y      = 0;
    glw_state->viewport.Width  = (DWORD)width;
    glw_state->viewport.Height = (DWORD)height;
    glw_state->viewport.MinZ   = 0.0f;
    glw_state->viewport.MaxZ   = 1.0f;

#ifdef _XBOX
    XBLog_Write("GLW_Init: calling wglCreateContext (fakegl owns D3D8 device)\n");
#endif
    s_fakeglContext = wglCreateContext();
    if (!s_fakeglContext) {
#ifdef _XBOX
        XBLog_Write("GLW_Init: wglCreateContext FAILED — fakegl couldn't init D3D8\n");
#endif
        return;
    }

#ifdef _XBOX
    {
        char b[64];
        _snprintf(b, sizeof(b), "GLW_Init: wglCreateContext OK ctx=%p\n", (void*)s_fakeglContext);
        b[sizeof(b)-1] = '\0';
        XBLog_Write(b);
    }
#endif

    wglMakeCurrent();
#ifdef _XBOX
    XBLog_Write("GLW_Init: wglMakeCurrent OK\n");

    /* CXBX-R cross-project audit divergence: gTextures.Initialize was
     * deferred out of Z_Init.  Now that fakegl has done Direct3DCreate8
     * + CreateDevice (above), NV2A pool is in a state where
     * D3D_AllocContiguousMemory is safe.  20 MB matches
     * z_memman_console.cpp's TEXTURE_POOL_SIZE. */
    XBLog_Write("GLW_Init: gTextures.Initialize (deferred from Z_Init)...\n");
    gTextures.Initialize(20 * 1024 * 1024);
    XBLog_Write("GLW_Init: gTextures.Initialize done\n");
#endif
    /* Plan-B audit gotcha E was attempted (FakeGL_GetD3DDevice → glw_state->device
     * wiring) but the supporting fakeglx.cpp additions correlated with a
     * wglCreateContext regression on CXBX-R LLE GPU.  Reverted; glw_state->device
     * stays NULL for now — Plan-C will revisit via a fakegl-free path. */

    /* Initial GL state — mirrors OpenJKDF2 std3D.c:373-383.
     * fakegl lazily calls BeginScene on first glBegin (m_needBeginScene). */
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (GLdouble)width, (GLdouble)height, 0.0, -99999.0, 99999.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_ALPHA_TEST);

#ifdef _XBOX
    XBLog_Write("GLW_Init: initial GL state set; skipping stability loop\n");

    /* Plan-B: stability loop REMOVED.
     *
     * OpenJKDF2's std3D.c:387-398 runs a 60-frame `glClear+FakeSwapBuffers`
     * loop right here.  On CXBX-R LLE GPU that pattern hangs in the very
     * first FakeSwapBuffers call: SwapBuffers→EndScene without any prior
     * BeginScene (fakegl's BeginScene fires lazily on first glBegin, but
     * the loop never calls glBegin).
     *
     * The loop is purely diagnostic per OpenJKDF2's own comment
     * ("confirms FakeGL's frame loop is alive") and not architecturally
     * required.  SP_DoLicense's first qglBeginEXT triggers BeginScene
     * naturally; its qglEndFrame then calls FakeSwapBuffers with the
     * scene properly open → EndScene paired correctly.  Removing the
     * loop avoids the unpaired EndScene without changing draw semantics. */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);  /* one-time clear color set */

    /* Plan-B: parallel CreateDevice removed (was a workaround for what
     * we believed was a vsync hang).  Root cause was actually XDK 5849
     * vs 5558 xboxkrnl ABI mismatch — fixed at the build script level
     * (all libs now resolve from XDK 5558 via /LIBPATH).  glw_state->device
     * stays NULL; the 150 JKA direct-D3D sites in tr_backend etc. will
     * crash post-license — those are Plan-C scope (rewrite to gl_*). */
    XBLog_Write("GLW_Init: parallel CreateDevice removed (lib-version fix supersedes)\n");

    XBLog_Write("GLW_Init: complete\n");
#endif
}

void GLW_Shutdown( void )
{
    if (s_fakeglContext) {
        wglDeleteContext();
        s_fakeglContext = NULL;
    }
}

/* =========================================================================
 *  Plan-B fix: GLimp_Init / GLimp_Shutdown / GLimp_EndFrame /
 *  GLimp_LogComment are NOT defined here.  They're already defined in
 *  win_glimp_console.cpp (lines 151, 180, 215, 234 etc.).  Defining
 *  them again here produced LNK4006 warnings under /FORCE:MULTIPLE;
 *  the linker discarded our versions and kept win_glimp_console's,
 *  which is what we want (it has the proper init flow that calls
 *  GLW_StartOpenGL → GLW_LoadOpenGL → GLW_CreateWindow → GLW_Init).
 *
 *  GLimp_EndFrame in win_glimp_console.cpp is currently empty.  If we
 *  need FakeSwapBuffers there, edit that file instead of redefining.
 * ========================================================================= */
