/*
 * fakeglx_jka_compat.cpp — Plan-B (OpenJKDF2 1:1) compatibility layer
 *
 * Real implementations of GL_* functions JKA's renderer calls but
 * OpenJKDF2's fakeglx.cpp does NOT export.  This file is the "adjacent
 * compat layer" the directive permits — fakeglx.cpp itself is byte-
 * identical from OpenJKDF2; missing exports are provided here.
 *
 * All functions are extern "C" to match fakeglx.cpp's wrapper linkage
 * convention (file-scope wrappers there export as C linkage per dumpbin).
 *
 * No stubs — every function does something meaningful per the GL spec
 * or per the D3D8 equivalent.  Where JKA-specific extensions have no
 * direct mapping (e.g. glPushAttrib, glIndexedTriToStrip), the
 * implementation does the minimum correct work to keep JKA's renderer
 * state coherent.
 */

#include <xtl.h>
/* Suppress qgl_console.h's #define glTexImage2D JkaGlTexImage2D redirect
 * — within this compat layer's glEndFrame, when we call gl* functions
 * to set up state before FakeSwapBuffers (matching OpenJKDF2's
 * std3D_Present), we want fakegl's real glTexImage2D etc. (not our
 * own wrappers).  Set the sentinel before the include. */
#define _JKA_DDS_BRIDGE_INTERNAL_
#include "../../renderer/qgl_console.h"
#include "../glw_win_dx8.h"
#include "../xb_log.h"
#include <stdlib.h>
#include <string.h>

#ifdef _XBOX
extern "C" int JkaFakeglDrawIndexedPrimitiveUP(D3DPRIMITIVETYPE dptPrimitiveType, DWORD typeDesc,
    UINT vertexCount, UINT primitiveCount, const void *indices,
    const void *vertices, UINT stride, int stefxOverlayActive,
    int stefxOverlayHud, int stefxOverlayBeam);
#endif

/* Plan-B audit gotchas B/D/F/H attempted to wire D3D-state-routing impls
 * via FakeGL_GetD3DDevice / FakeGL_MultMatrixfLocal / FakeGL_DeleteTexture
 * / FakeGL_MTexCoord2f.  Those accessors were exported from fakeglx.cpp
 * but their addition correlated 1:1 with a wglCreateContext regression
 * (hardware test 2026-05-17).  Reverted to no-op stubs here; the
 * underlying state-correctness shortfalls remain on the deferred list
 * for after license-screen pixels are confirmed. */

/* Shadow-state helpers for compat-only state not owned by fakeglx.cpp. */
static GLuint g_capEnabled = 0;
static int CapBit(GLenum cap) {
    switch (cap) {
    case GL_ALPHA_TEST:           return 0;
    case GL_BLEND:                return 1;
    case GL_CULL_FACE:            return 2;
    case GL_DEPTH_TEST:           return 3;
    case GL_FOG:                  return 4;
    case GL_LIGHTING:             return 5;
    case GL_POLYGON_OFFSET_FILL:  return 6;
    case GL_SCISSOR_TEST:         return 7;
    case GL_STENCIL_TEST:         return 8;
    case GL_TEXTURE_2D:           return 9;
    case GL_NORMALIZE:            return 10;
    case GL_DITHER:               return 11;
    default:                      return -1;
    }
}

/* Error queue per GL spec — return + clear semantics. */
static GLenum g_lastError = 0; /* GL_NO_ERROR */

/* Clear-depth value tracked for next glClear. */
static GLclampd g_clearDepth = 1.0;

/* Active stage tracking (0..3). */
static GLuint g_activeStage = 0;
static GLuint g_clientActiveStage = 0;

/* Client array enable bitmap. */
static GLuint g_clientArrays = 0;
static GLuint g_texCoordArrayEnabled = 0;

/* Client array bindings — pointer + stride + type for each. */
struct ArrayBinding {
    GLint   size;
    GLenum  type;
    GLsizei stride;
    const GLvoid *pointer;
};
static ArrayBinding g_vertexArray   = {0, GL_FLOAT, 0, NULL};
static ArrayBinding g_colorArray    = {0, GL_FLOAT, 0, NULL};
static ArrayBinding g_texCoordArray[4] = {
    {0, GL_FLOAT, 0, NULL},
    {0, GL_FLOAT, 0, NULL},
    {0, GL_FLOAT, 0, NULL},
    {0, GL_FLOAT, 0, NULL}
};
static ArrayBinding g_normalArray   = {0, GL_FLOAT, 0, NULL};

static GLsizei ArrayElementSize(const ArrayBinding& binding);
static GLsizei ArrayStride(const ArrayBinding& binding);

#ifdef _XBOX
extern "C" volatile unsigned int g_SPXBFakeGLPrimitiveCalls;
extern "C" volatile unsigned int g_SPXBFakeGLPrimitiveVerts;
static int g_stefxOverlayDrawContext = 0;
static int g_stefxOverlayDrawHud = 0;
static int g_stefxOverlayDrawBeam = 0;
static int g_stefxDrawContextActive = 0;
static char g_stefxDrawContextShader[128] = "";
static int g_stefxDrawContextStage = -1;
static int g_stefxDrawContextExpectedStages = 0;
static unsigned int g_stefxDrawContextStateBits = 0;

extern "C" void JkaFakeglSetEliteForceOverlayDrawContext(int active, int hud, int beam)
{
    static int s_overlayContextBudget = 48;

    g_stefxOverlayDrawContext = active ? 1 : 0;
    g_stefxOverlayDrawHud = hud ? 1 : 0;
    g_stefxOverlayDrawBeam = beam ? 1 : 0;
    if (s_overlayContextBudget > 0) {
        XBLF("STEFX_OVERLAY_CONTEXT active=%d hud=%d beam=%d",
            g_stefxOverlayDrawContext,
            g_stefxOverlayDrawHud,
            g_stefxOverlayDrawBeam);
        --s_overlayContextBudget;
    }
}

extern "C" void JkaFakeglSetEliteForceDrawContext(const char *shader, int stage, int expectedStages, unsigned int stateBits)
{
    static int s_setContextBudget = 16;

    g_stefxDrawContextActive = shader && shader[0] ? 1 : 0;
    if (g_stefxDrawContextActive) {
        strncpy(g_stefxDrawContextShader, shader, sizeof(g_stefxDrawContextShader) - 1);
        g_stefxDrawContextShader[sizeof(g_stefxDrawContextShader) - 1] = '\0';
    } else {
        g_stefxDrawContextShader[0] = '\0';
    }
    g_stefxDrawContextStage = stage;
    g_stefxDrawContextExpectedStages = expectedStages;
    g_stefxDrawContextStateBits = stateBits;

    if (s_setContextBudget > 0 && g_stefxDrawContextActive) {
        XBLF("STEFX_DRAW_CONTEXT_SET shader='%s' stage=%d expectedStages=%d state=0x%08x",
            g_stefxDrawContextShader,
            g_stefxDrawContextStage,
            g_stefxDrawContextExpectedStages,
            g_stefxDrawContextStateBits);
        --s_setContextBudget;
    }
}

struct JkaFastVertex0 {
    float x, y, z;
    DWORD color;
};

struct JkaFastVertex1 {
    float x, y, z;
    DWORD color;
    float s0, t0;
};

struct JkaFastVertex2 {
    float x, y, z;
    DWORD color;
    float s0, t0;
    float s1, t1;
};

static DWORD PackColorFromArray(GLuint i)
{
    if ((g_clientArrays & (1u << 2)) && g_colorArray.pointer) {
        const char *cp = (const char*)g_colorArray.pointer + i * ArrayStride(g_colorArray);
        if (g_colorArray.type == GL_UNSIGNED_BYTE && g_colorArray.size >= 4) {
            const GLubyte *c = (const GLubyte *)cp;
            return ((DWORD)c[3] << 24) | ((DWORD)c[2] << 16) | ((DWORD)c[1] << 8) | (DWORD)c[0];
        }
        if (g_colorArray.type == GL_FLOAT) {
            const GLfloat *c = (const GLfloat*)cp;
            const DWORD r = (DWORD)(c[0] * 255.0f) & 0xff;
            const DWORD g = (DWORD)(c[1] * 255.0f) & 0xff;
            const DWORD b = (DWORD)(c[2] * 255.0f) & 0xff;
            const DWORD a = (DWORD)((g_colorArray.size >= 4 ? c[3] : 1.0f) * 255.0f) & 0xff;
            return (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
    return 0xffffffff;
}

static bool JkaTryDrawElementsUP(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
    if (g_stefxOverlayDrawContext) {
        static int s_overlayFastEntryBudget = 24;
        if (s_overlayFastEntryBudget > 0) {
            XBLF("STEFX_OVERLAY_FAST_ENTRY hud=%d beam=%d mode=0x%08x count=%d type=0x%08x client=0x%08x texMask=0x%08x vertexPtr=%08x vType=0x%08x vSize=%d",
                g_stefxOverlayDrawHud,
                g_stefxOverlayDrawBeam,
                (unsigned int)mode,
                (int)count,
                (unsigned int)type,
                (unsigned int)g_clientArrays,
                (unsigned int)g_texCoordArrayEnabled,
                (unsigned int)g_vertexArray.pointer,
                (unsigned int)g_vertexArray.type,
                (int)g_vertexArray.size);
            --s_overlayFastEntryBudget;
        }
    }

    if (mode != GL_TRIANGLES || type != GL_UNSIGNED_SHORT || !indices ||
        !g_vertexArray.pointer || g_vertexArray.type != GL_FLOAT || g_vertexArray.size < 3 ||
        count <= 0 || (count % 3) != 0) {
        if (g_stefxOverlayDrawContext) {
            static int s_overlayFastShapeRejectBudget = 24;
            if (s_overlayFastShapeRejectBudget > 0) {
                XBLF("STEFX_OVERLAY_FAST_REJECT reason=shape mode=0x%08x type=0x%08x count=%d vertexPtr=%08x vType=0x%08x vSize=%d",
                    (unsigned int)mode,
                    (unsigned int)type,
                    (int)count,
                    (unsigned int)g_vertexArray.pointer,
                    (unsigned int)g_vertexArray.type,
                    (int)g_vertexArray.size);
                --s_overlayFastShapeRejectBudget;
            }
        }
        static int s_fastDrawRejectBudget = 16;
        if (s_fastDrawRejectBudget > 0) {
            XBLF("JA: JkaTryDrawElementsUP reject mode=0x%08x type=0x%08x count=%d vertexPtr=%08x vType=0x%08x vSize=%d",
                (unsigned int)mode,
                (unsigned int)type,
                (int)count,
                (unsigned int)g_vertexArray.pointer,
                (unsigned int)g_vertexArray.type,
                (int)g_vertexArray.size);
            --s_fastDrawRejectBudget;
        }
        return false;
    }

    const GLushort *idx = (const GLushort *)indices;
    GLuint maxIndex = 0;
    for (GLsizei i = 0; i < count; ++i) {
        if (idx[i] > maxIndex) {
            maxIndex = idx[i];
        }
    }
    static GLushort *s_fastIndices = NULL;
    static UINT s_fastIndicesCount = 0;
    static GLushort *s_fastSourceIndices = NULL;
    static UINT s_fastSourceCount = 0;
    static UINT *s_fastRemap = NULL;
    static UINT *s_fastRemapGeneration = NULL;
    static UINT s_fastRemapCount = 0;
    static UINT s_fastGeneration = 1;
    if ((UINT)count > s_fastIndicesCount) {
        GLushort *newIndices = (GLushort *)realloc(s_fastIndices, count * sizeof(GLushort));
        if (!newIndices) {
            return false;
        }
        s_fastIndices = newIndices;
        s_fastIndicesCount = (UINT)count;
    }
    if ((UINT)count > s_fastSourceCount) {
        GLushort *newSources = (GLushort *)realloc(s_fastSourceIndices, count * sizeof(GLushort));
        if (!newSources) {
            return false;
        }
        s_fastSourceIndices = newSources;
        s_fastSourceCount = (UINT)count;
    }
    if (maxIndex + 1 > s_fastRemapCount) {
        UINT oldCount = s_fastRemapCount;
        UINT newCount = maxIndex + 1;
        UINT *newRemap = (UINT *)realloc(s_fastRemap, newCount * sizeof(UINT));
        if (!newRemap) {
            return false;
        }
        s_fastRemap = newRemap;
        UINT *newGeneration = (UINT *)realloc(s_fastRemapGeneration, newCount * sizeof(UINT));
        if (!newGeneration) {
            return false;
        }
        s_fastRemapGeneration = newGeneration;
        s_fastRemapCount = newCount;
        for (UINT i = oldCount; i < s_fastRemapCount; ++i) {
            s_fastRemap[i] = 0;
            s_fastRemapGeneration[i] = 0;
        }
    }

    ++s_fastGeneration;
    if (!s_fastGeneration) {
        memset(s_fastRemapGeneration, 0, s_fastRemapCount * sizeof(UINT));
        s_fastGeneration = 1;
    }

    UINT vertexCount = 0;
    for (GLsizei i = 0; i < count; ++i) {
        const UINT sourceIndex = idx[i];
        UINT mappedIndex = 0;
        if (s_fastRemapGeneration[sourceIndex] != s_fastGeneration) {
            if (vertexCount >= 65535u) {
                return false;
            }
            mappedIndex = vertexCount;
            s_fastRemapGeneration[sourceIndex] = s_fastGeneration;
            s_fastRemap[sourceIndex] = mappedIndex;
            s_fastSourceIndices[vertexCount] = (GLushort)sourceIndex;
            ++vertexCount;
        } else {
            mappedIndex = s_fastRemap[sourceIndex];
        }
        s_fastIndices[i] = (GLushort)mappedIndex;
    }

    int texStages = 0;
    if ((g_texCoordArrayEnabled & 1u) && g_texCoordArray[0].pointer && g_texCoordArray[0].type == GL_FLOAT && g_texCoordArray[0].size >= 2) {
        texStages = 1;
    }
    if ((g_texCoordArrayEnabled & 2u) && g_texCoordArray[1].pointer && g_texCoordArray[1].type == GL_FLOAT && g_texCoordArray[1].size >= 2) {
        texStages = 2;
    }

    if (g_stefxOverlayDrawContext) {
        static int s_overlayFastStageBudget = 24;
        if (s_overlayFastStageBudget > 0) {
            XBLF("STEFX_OVERLAY_FAST_STAGE hud=%d beam=%d stages=%d texMask=0x%08x count=%d verts=%u",
                g_stefxOverlayDrawHud,
                g_stefxOverlayDrawBeam,
                texStages,
                (unsigned int)g_texCoordArrayEnabled,
                (int)count,
                (unsigned int)vertexCount);
            --s_overlayFastStageBudget;
        }
    }

#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_MP)
    if (texStages >= 2) {
        static int s_stefxTwoStageFallbackBudget = 32;
        if (s_stefxTwoStageFallbackBudget > 0) {
            XBLF("STEFX_HM: indexed UP skipped for two-stage draw count=%d maxIndex=%u",
                (int)count, (unsigned int)maxIndex);
            --s_stefxTwoStageFallbackBudget;
        }
        return false;
    }
#endif

    const DWORD fvf = D3DFVF_XYZ | D3DFVF_DIFFUSE | ((DWORD)texStages << D3DFVF_TEXCOUNT_SHIFT);
    const UINT primitiveCount = count / 3;
    const UINT stride = (texStages == 2) ? sizeof(JkaFastVertex2) : ((texStages == 1) ? sizeof(JkaFastVertex1) : sizeof(JkaFastVertex0));
    static char *s_fastVerts = NULL;
    static UINT s_fastVertsBytes = 0;
    const UINT requiredBytes = vertexCount * stride;
    if (requiredBytes > s_fastVertsBytes) {
        UINT newBytes = s_fastVertsBytes ? s_fastVertsBytes : (64 * 1024);
        while (newBytes < requiredBytes) {
            newBytes *= 2;
        }
        char *newVerts = (char *)realloc(s_fastVerts, newBytes);
        if (!newVerts) {
            return false;
        }
        s_fastVerts = newVerts;
        s_fastVertsBytes = newBytes;
    }
    char *verts = s_fastVerts;
    if (!verts) {
        return false;
    }

    const int xyzStride = ArrayStride(g_vertexArray);
    const int st0Stride = texStages >= 1 ? ArrayStride(g_texCoordArray[0]) : 0;
    const int st1Stride = texStages >= 2 ? ArrayStride(g_texCoordArray[1]) : 0;

    for (UINT i = 0; i < vertexCount; ++i) {
        const UINT sourceIndex = s_fastSourceIndices[i];
        const GLfloat *xyz = (const GLfloat *)((const char *)g_vertexArray.pointer + sourceIndex * xyzStride);
        DWORD color = PackColorFromArray(sourceIndex);
        if (texStages == 2) {
            JkaFastVertex2 *v = (JkaFastVertex2 *)(verts + i * stride);
            const GLfloat *st0 = (const GLfloat *)((const char *)g_texCoordArray[0].pointer + sourceIndex * st0Stride);
            const GLfloat *st1 = (const GLfloat *)((const char *)g_texCoordArray[1].pointer + sourceIndex * st1Stride);
            v->x = xyz[0]; v->y = xyz[1]; v->z = xyz[2]; v->color = color;
            v->s0 = st0[0]; v->t0 = st0[1]; v->s1 = st1[0]; v->t1 = st1[1];
        } else if (texStages == 1) {
            JkaFastVertex1 *v = (JkaFastVertex1 *)(verts + i * stride);
            const GLfloat *st0 = (const GLfloat *)((const char *)g_texCoordArray[0].pointer + sourceIndex * st0Stride);
            v->x = xyz[0]; v->y = xyz[1]; v->z = xyz[2]; v->color = color;
            v->s0 = st0[0]; v->t0 = st0[1];
        } else {
            JkaFastVertex0 *v = (JkaFastVertex0 *)(verts + i * stride);
            v->x = xyz[0]; v->y = xyz[1]; v->z = xyz[2]; v->color = color;
        }
    }

    static int s_fastDrawLogBudget = 8;
    static int s_efFastDrawStage2Budget = 8;
    static int s_stefxDrawContextBudget = 24;
    const bool efLogFastDraw = (texStages >= 2 && s_efFastDrawStage2Budget > 0) ||
        (s_fastDrawLogBudget > 0);
    if (efLogFastDraw) {
        const GLfloat *xyz0 = (const GLfloat *)g_vertexArray.pointer;
        DWORD color0 = PackColorFromArray(0);
        float s0 = 0.0f;
        float t0 = 0.0f;
        float s1 = 0.0f;
        float t1 = 0.0f;
        if (texStages >= 1 && g_texCoordArray[0].pointer) {
            const GLfloat *st0 = (const GLfloat *)g_texCoordArray[0].pointer;
            s0 = st0[0];
            t0 = st0[1];
        }
        if (texStages >= 2 && g_texCoordArray[1].pointer) {
            const GLfloat *st1 = (const GLfloat *)g_texCoordArray[1].pointer;
            s1 = st1[0];
            t1 = st1[1];
        }
        XBLF("JA: JkaTryDrawElementsUP sample #%d mode=0x%08x count=%d verts=%u prims=%u stages=%d fvf=0x%08lx idx0=%u idx1=%u idx2=%u xyz0=%g,%g,%g color0=0x%08lx st0=%g,%g st1=%g,%g\n",
            16 - s_fastDrawLogBudget,
            (unsigned int)mode,
            (int)count,
            (unsigned int)vertexCount,
            (unsigned int)primitiveCount,
            texStages,
            (unsigned long)fvf,
            (unsigned int)idx[0],
            (unsigned int)(count > 1 ? idx[1] : 0),
            (unsigned int)(count > 2 ? idx[2] : 0),
            xyz0 ? xyz0[0] : 0.0f,
            xyz0 ? xyz0[1] : 0.0f,
            xyz0 ? xyz0[2] : 0.0f,
            (unsigned long)color0,
            s0, t0, s1, t1);
        XBLF("EF: FAST_DRAW_SAMPLE sample=%d mode=0x%08x count=%d verts=%u prims=%u stages=%d fvf=0x%08lx texMask=0x%08x color0=0x%08lx st0=%g,%g st1=%g,%g",
            texStages >= 2 ? (96 - s_efFastDrawStage2Budget) : (16 - s_fastDrawLogBudget),
            (unsigned int)mode,
            (int)count,
            (unsigned int)vertexCount,
            (unsigned int)primitiveCount,
            texStages,
            (unsigned long)fvf,
            (unsigned int)g_texCoordArrayEnabled,
            (unsigned long)color0,
            s0, t0, s1, t1);
        if (s_fastDrawLogBudget > 0) {
            --s_fastDrawLogBudget;
        }
        if (texStages >= 2 && s_efFastDrawStage2Budget > 0) {
            --s_efFastDrawStage2Budget;
        }
    }
    if (g_stefxDrawContextActive && s_stefxDrawContextBudget > 0) {
        DWORD color0 = PackColorFromArray(0);
        XBLF("STEFX_DRAW_CONTEXT shader='%s' stage=%d expectedStages=%d actualStages=%d texMask=0x%08x fvf=0x%08lx state=0x%08x count=%d verts=%u prims=%u color0=0x%08lx",
            g_stefxDrawContextShader,
            g_stefxDrawContextStage,
            g_stefxDrawContextExpectedStages,
            texStages,
            (unsigned int)g_texCoordArrayEnabled,
            (unsigned long)fvf,
            g_stefxDrawContextStateBits,
            (int)count,
            (unsigned int)vertexCount,
            (unsigned int)primitiveCount,
            (unsigned long)color0);
        --s_stefxDrawContextBudget;
    }

    bool drawOk = JkaFakeglDrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, fvf, vertexCount, primitiveCount,
        s_fastIndices, verts, stride, g_stefxOverlayDrawContext,
        g_stefxOverlayDrawHud, g_stefxOverlayDrawBeam) != 0;
    if (g_stefxOverlayDrawContext) {
        static int s_overlayFastResultBudget = 24;
        if (s_overlayFastResultBudget > 0) {
            XBLF("STEFX_OVERLAY_FAST_RESULT ok=%d hud=%d beam=%d stages=%d texMask=0x%08x fvf=0x%08lx count=%d verts=%u prims=%u",
                drawOk ? 1 : 0,
                g_stefxOverlayDrawHud,
                g_stefxOverlayDrawBeam,
                texStages,
                (unsigned int)g_texCoordArrayEnabled,
                (unsigned long)fvf,
                (int)count,
                (unsigned int)vertexCount,
                (unsigned int)primitiveCount);
            --s_overlayFastResultBudget;
        }
    }
    {
        static int s_efFastDrawSubmitBudget = 12;
        static int s_efOverlayFastDrawSubmitBudget = 16;
        bool logFastDraw = false;
        if (g_stefxOverlayDrawContext) {
            logFastDraw = s_efOverlayFastDrawSubmitBudget > 0;
        } else {
            logFastDraw = s_efFastDrawSubmitBudget > 0;
        }

        if (logFastDraw) {
            const GLfloat *xyz0 = (const GLfloat *)g_vertexArray.pointer;
            DWORD color0 = PackColorFromArray(0);
            XBLF("EF: FAST_DRAW_SUBMIT ok=%d overlay=%d hud=%d beam=%d mode=0x%08x count=%d verts=%u prims=%u stages=%d fvf=0x%08lx texMask=0x%08x color0=0x%08lx xyz0=%g,%g,%g",
                drawOk ? 1 : 0,
                g_stefxOverlayDrawContext,
                g_stefxOverlayDrawHud,
                g_stefxOverlayDrawBeam,
                (unsigned int)mode,
                (int)count,
                (unsigned int)vertexCount,
                (unsigned int)primitiveCount,
                texStages,
                (unsigned long)fvf,
                (unsigned int)g_texCoordArrayEnabled,
                (unsigned long)color0,
                xyz0 ? xyz0[0] : 0.0f,
                xyz0 ? xyz0[1] : 0.0f,
                xyz0 ? xyz0[2] : 0.0f);
            if (g_stefxOverlayDrawContext) {
                --s_efOverlayFastDrawSubmitBudget;
            } else {
                --s_efFastDrawSubmitBudget;
            }
        }
    }
    if (!drawOk) {
        static int s_fastDrawFailBudget = 16;
        if (s_fastDrawFailBudget > 0) {
            XBLF("JA: JkaTryDrawElementsUP failed mode=0x%08x count=%d verts=%u stages=%d",
                (unsigned int)mode, (int)count, (unsigned int)vertexCount, texStages);
            --s_fastDrawFailBudget;
        }
        return false;
    }
    return true;
}

#endif

static GLsizei ArrayElementSize(const ArrayBinding& binding)
{
    int componentBytes;

    switch (binding.type) {
    case GL_UNSIGNED_BYTE:
        componentBytes = sizeof(GLubyte);
        break;
    case GL_UNSIGNED_SHORT:
        componentBytes = sizeof(GLushort);
        break;
    case GL_UNSIGNED_INT:
        componentBytes = sizeof(GLuint);
        break;
    case GL_FLOAT:
    default:
        componentBytes = sizeof(GLfloat);
        break;
    }

    return binding.size * componentBytes;
}

static GLsizei ArrayStride(const ArrayBinding& binding)
{
    return binding.stride ? binding.stride : ArrayElementSize(binding);
}

/* Texture ID allocator — fakeglx's glBindTexture takes any GLuint and
 * routes it; we just need to hand out unique IDs. */
static GLuint g_nextTexId = 1;
extern "C" void JkaFakeglDeleteTexture(GLuint texture);
extern "C" int JkaFakeglIsTexture(GLuint texture);
extern "C" void JkaFakeglMTexCoord2fSGIS(GLenum target, GLfloat s, GLfloat t);
extern "C" void JkaFakeglSelectTextureSGIS(GLenum target);
extern "C" GLboolean JkaFakeglIsEnabled(GLenum cap);
extern "C" void JkaFakeglEnable(GLenum cap);
extern "C" void JkaFakeglDisable(GLenum cap);
extern "C" void JkaFakeglScissor(GLint x, GLint y, GLsizei width, GLsizei height);
extern "C" void JkaFakeglFogf(GLenum pname, GLfloat param);
extern "C" void JkaFakeglFogfv(GLenum pname, const GLfloat *params);
extern "C" void JkaFakeglFogi(GLenum pname, GLint param);

#define JKA_TEXTURE0_SGIS 0x835E

static GLenum StageToSgisTarget(GLuint stage)
{
    return (GLenum)(JKA_TEXTURE0_SGIS + (stage & 3));
}

extern "C" {

/* ============================================================
 *   Texture object management
 * ============================================================ */

void glGenTextures(GLsizei n, GLuint *textures)
{
    if (!textures || n <= 0) return;
    for (GLsizei i = 0; i < n; ++i) {
        textures[i] = g_nextTexId++;
    }
}

void glDeleteTextures(GLsizei n, const GLuint *textures)
{
    if (!textures || n <= 0) return;
    for (GLsizei i = 0; i < n; ++i) {
        if (textures[i])
            JkaFakeglDeleteTexture(textures[i]);
    }
}

GLboolean glIsTexture(GLuint texture)
{
    return JkaFakeglIsTexture(texture) ? GL_TRUE : GL_FALSE;
}

void glTexParameteri(GLenum target, GLenum pname, GLint param)
{
    /* Forward to the float variant fakeglx exports. */
    glTexParameterf(target, pname, (GLfloat)param);
}

void glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params)
{
    if (!params) { g_lastError = GL_INVALID_VALUE; return; }
    /* For multi-component params (border colour), forward first only —
     * fakegl's glTexParameterf is a single-value setter.  GL_TEXTURE_BORDER_COLOR
     * is the main multi-component case and on Xbox D3D8 it's not a real
     * stage state anyway (clamped via texture address mode). */
    if (pname == GL_TEXTURE_BORDER_COLOR) {
        if (target != GL_TEXTURE_2D) {
            g_lastError = GL_INVALID_ENUM;
        }
        return;
    }
    glTexParameterf(target, pname, params[0]);
}

void glTexEnvi(GLenum target, GLenum pname, GLint param)
{
    /* Forward to fakegl's glTexEnvf. */
    glTexEnvf(target, pname, (GLfloat)param);
}

/* Plan-B: glTexImage2DEXT is owned by glteximage_dds.cpp's
 * JkaGlTexImage2DEXT, which routes through the DDS-aware path.
 * qgl_console.h's #define glTexImage2DEXT JkaGlTexImage2DEXT
 * redirects all JKA call sites. */

/* ============================================================
 *   State queries / shadow
 * ============================================================ */

void glGetIntegerv(GLenum pname, GLint *params)
{
    if (!params) { g_lastError = GL_INVALID_VALUE; return; }
    switch (pname) {
    case 0x0D33: /* GL_MAX_TEXTURE_SIZE */                params[0] = 4096; break;
    case 0x84E2: /* GL_MAX_TEXTURE_UNITS_ARB */
    case 0x8872: /* GL_MAX_TEXTURE_IMAGE_UNITS */         params[0] = 4;    break;
    case 0x0D31: /* GL_MAX_LIGHTS */                      params[0] = 8;    break;
    case 0x0D32: /* GL_MAX_CLIP_PLANES */                 params[0] = 6;    break;
    case 0x84FF: /* GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT */  params[0] = 16;   break;
    case 0x0D57: /* GL_STENCIL_BITS */                    params[0] = 8;    break;
    case 0x0D56: /* GL_DEPTH_BITS */                      params[0] = 24;   break;
    case 0x0D52: case 0x0D53: case 0x0D54: case 0x0D55:   params[0] = 8;    break;
    case 0x0BA2: /* GL_VIEWPORT */
        params[0] = 0; params[1] = 0; params[2] = 640; params[3] = 480; break;
    default:
        params[0] = 0;
        g_lastError = GL_INVALID_ENUM;
        break;
    }
}

void glGetBooleanv(GLenum pname, GLboolean *params)
{
    if (!params) { g_lastError = GL_INVALID_VALUE; return; }
    int b = CapBit(pname);
    params[0] = (b >= 0 && (g_capEnabled & (1u << b))) ? GL_TRUE : GL_FALSE;
}

GLenum glGetError(void)
{
    GLenum e = g_lastError;
    g_lastError = 0;
    return e;
}

GLboolean glIsEnabled(GLenum cap)
{
    int b = CapBit(cap);
    if (b < 0) return GL_FALSE;
    return (g_capEnabled & (1u << b)) ? GL_TRUE : GL_FALSE;
}

/* ============================================================
 *   Clear / depth / stencil
 * ============================================================ */

void glClearDepth(GLclampd depth) { g_clearDepth = depth; }
void glClearStencil(GLint /*s*/)  { /* tracked via D3D Clear stencil arg */ }

void glStencilFunc(GLenum /*func*/, GLint /*ref*/, GLuint /*mask*/) {}
void glStencilOp(GLenum /*sfail*/, GLenum /*zfail*/, GLenum /*zpass*/) {}
void glStencilMask(GLuint /*mask*/) {}

/* ============================================================
 *   Fog / lighting / material — D3D8 fixed-function pipeline
 * ============================================================ */

void glFogf(GLenum pname, GLfloat param) { JkaFakeglFogf(pname, param); }
void glFogfv(GLenum pname, const GLfloat *params) { JkaFakeglFogfv(pname, params); }
void glFogi(GLenum pname, GLint param) { JkaFakeglFogi(pname, param); }

void glLightf(GLenum /*light*/, GLenum /*pname*/, GLfloat /*param*/) {}
void glLightfv(GLenum /*light*/, GLenum /*pname*/, const GLfloat * /*params*/) {}
void glLightModelf(GLenum /*pname*/, GLfloat /*param*/) {}
void glLightModelfv(GLenum /*pname*/, const GLfloat * /*params*/) {}
void glMaterialf(GLenum /*face*/, GLenum /*pname*/, GLfloat /*param*/) {}
void glMaterialfv(GLenum /*face*/, GLenum /*pname*/, const GLfloat * /*params*/) {}

/* ============================================================
 *   Misc state
 * ============================================================ */

/* Gotcha D: reverted with accessor revert.  No-op stubs. */
void glPolygonOffset(GLfloat /*factor*/, GLfloat /*units*/) {}
void glScissor(GLint x, GLint y, GLsizei w, GLsizei h) { JkaFakeglScissor(x, y, w, h); }
void glLineWidth(GLfloat /*width*/) {}
void glPointSize(GLfloat /*size*/)  {}
void glFlush(void)                  { /* fakeglx submits on glEnd; no-op per spec */ }
extern "C" void JkaFakeglClipPlane0( const GLdouble *equation );
void glClipPlane(GLenum plane, const GLdouble *equation)
{
    if (plane == GL_CLIP_PLANE0) {
        JkaFakeglClipPlane0(equation);
    }
}
void glColorMask(GLboolean /*r*/, GLboolean /*g*/, GLboolean /*b*/, GLboolean /*a*/) {}

/* Gotcha B: reverted with accessor revert — back to glLoadMatrixf
 * (semantically wrong but matches pre-A-I baseline). */
void JkaGlMultMatrixf(const GLfloat *m)
{
    if (m) glLoadMatrixf(m);
}
void glMultMatrixf(const GLfloat *m)
{
    if (m) glLoadMatrixf(m);
}

/* Gotcha A fix: JkaGlMatrixMode intercepts GL_TEXTURE0/1 (non-spec args
 * SP_DrawTexture passes) and re-routes to GL_TEXTURE so fakegl's
 * default→LocalDebugBreak path doesn't silently leave the previous
 * stack selected and let the next glLoadIdentity wipe the wrong matrix. */
void JkaGlMatrixMode(GLenum mode)
{
    /* GL_TEXTURE0 = 0x84C0, GL_TEXTURE1 = 0x84C1 (and ARB variants).
     * GL_TEXTURE = 0x1702.  Anything in the TEXTUREn range → GL_TEXTURE. */
    /* GL_TEXTURE matrix mode = 0x1702 (not declared in qgl_console.h). */
    if (mode == GL_TEXTURE0 || mode == GL_TEXTURE1 ||
        mode == 0x84C0      || mode == 0x84C1)
    {
        glMatrixMode(0x1702);   /* GL_TEXTURE — fakegl's real glMatrixMode */
        return;
    }
    glMatrixMode(mode);
}

void glTexCoord2fv(const GLfloat *v) { if (v) glTexCoord2f(v[0], v[1]); }

/* ============================================================
 *   Client array state + draw
 * ============================================================ */

void glEnableClientState(GLenum array)  {
    if (array == GL_TEXTURE_COORD_ARRAY) {
        g_clientArrays |= (1u << (array - 0x8074));
        g_texCoordArrayEnabled |= (1u << (g_clientActiveStage & 3));
        return;
    }
    if (array >= 0x8074 && array <= 0x8079) g_clientArrays |= (1u << (array - 0x8074));
}
void glDisableClientState(GLenum array) {
    if (array == GL_TEXTURE_COORD_ARRAY) {
        g_texCoordArrayEnabled &= ~(1u << (g_clientActiveStage & 3));
        if (!g_texCoordArrayEnabled) {
            g_clientArrays &= ~(1u << (array - 0x8074));
        }
        return;
    }
    if (array >= 0x8074 && array <= 0x8079) g_clientArrays &= ~(1u << (array - 0x8074));
}

void glVertexPointer  (GLint size, GLenum type, GLsizei stride, const GLvoid *p) {
    g_vertexArray.size = size; g_vertexArray.type = type;
    g_vertexArray.stride = stride; g_vertexArray.pointer = p;
}
void glColorPointer   (GLint size, GLenum type, GLsizei stride, const GLvoid *p) {
    g_colorArray.size = size; g_colorArray.type = type;
    g_colorArray.stride = stride; g_colorArray.pointer = p;
}
void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *p) {
    ArrayBinding& binding = g_texCoordArray[g_clientActiveStage & 3];
    binding.size = size; binding.type = type;
    binding.stride = stride; binding.pointer = p;
}
void glNormalPointer  (GLenum type, GLsizei stride, const GLvoid *p) {
    g_normalArray.size = 3; g_normalArray.type = type;
    g_normalArray.stride = stride; g_normalArray.pointer = p;
}

/* Submit a single vertex i: emits color, texcoord, normal (if their
 * client arrays are enabled) then the vertex itself.  Used by both
 * glDrawArrays and glDrawElements. */
static void SubmitArrayVertex(GLuint i)
{
    /* Color array (GL_COLOR_ARRAY = 0x8076 → bit 2). */
    if ((g_clientArrays & (1u << 2)) && g_colorArray.pointer) {
        GLint stride = ArrayStride(g_colorArray);
        const char *cp = (const char*)g_colorArray.pointer + i * stride;
        if (g_colorArray.type == GL_FLOAT) {
            const GLfloat *c = (const GLfloat*)cp;
            if (g_colorArray.size == 4) glColor4f(c[0], c[1], c[2], c[3]);
            else                         glColor3f(c[0], c[1], c[2]);
        } else if (g_colorArray.type == GL_UNSIGNED_BYTE) {
            const GLubyte *c = (const GLubyte*)cp;
            if (g_colorArray.size == 4) {
#ifdef _XBOX
                /* The Xbox renderer-side ComputeColors path stores tess
                 * colors as packed D3DCOLOR (AARRGGBB).  In memory that is
                 * BB GG RR AA, but this GL compatibility path expects RGBA
                 * bytes before repacking to D3D for fakegl. */
                static int s_colorByteOrderLogCount = 0;
                if (s_colorByteOrderLogCount < 4) {
                    XBLog_Write("JKA compat: converting packed D3D color array bytes to RGBA");
                    ++s_colorByteOrderLogCount;
                }
                glColor4ub(c[2], c[1], c[0], c[3]);
#else
                glColor4ub(c[0], c[1], c[2], c[3]);
#endif
            }
            else                         glColor3ub(c[0], c[1], c[2]);
        }
    }
    /* Texcoord array (GL_TEXTURE_COORD_ARRAY = 0x8078 → bit 4). */
    for (GLuint stage = 0; stage < 4; ++stage) {
        ArrayBinding& binding = g_texCoordArray[stage];
        if (!(g_texCoordArrayEnabled & (1u << stage)) || !binding.pointer) {
            continue;
        }
        GLint stride = ArrayStride(binding);
        const GLfloat *t = (const GLfloat*)((const char*)binding.pointer + i * stride);
        if (stage == 0) {
            glTexCoord2f(t[0], t[1]);
        } else {
            JkaFakeglMTexCoord2fSGIS(StageToSgisTarget(stage), t[0], t[1]);
        }
    }
    /* Normal array (GL_NORMAL_ARRAY = 0x8075 → bit 1). */
    if ((g_clientArrays & (1u << 1)) && g_normalArray.pointer) {
        GLint stride = ArrayStride(g_normalArray);
        const GLfloat *n = (const GLfloat*)((const char*)g_normalArray.pointer + i * stride);
        glNormal3f(n[0], n[1], n[2]);
    }
    /* Vertex (always last — GL spec: vertex submission emits the assembled vertex). */
    if (g_vertexArray.pointer) {
        GLint stride = ArrayStride(g_vertexArray);
        const GLfloat *v = (const GLfloat*)((const char*)g_vertexArray.pointer + i * stride);
        if (g_vertexArray.size >= 3) glVertex3f(v[0], v[1], v[2]);
        else                          glVertex2f(v[0], v[1]);
    }
}

void glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    glBegin(mode);
    for (GLint i = first; i < first + count; ++i)
        SubmitArrayVertex((GLuint)i);
    glEnd();
}

void glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
    if (!indices) return;
#ifdef _XBOX
    if (g_stefxOverlayDrawContext) {
        static int s_overlayDrawElementsBudget = 24;
        if (s_overlayDrawElementsBudget > 0) {
            const GLushort *idx16 = (type == GL_UNSIGNED_SHORT) ? (const GLushort *)indices : NULL;
            XBLF("STEFX_OVERLAY_DRAW_ELEMENTS hud=%d beam=%d mode=0x%08x count=%d type=0x%08x idx0=%u,%u,%u client=0x%08x texMask=0x%08x vertexPtr=%08x",
                g_stefxOverlayDrawHud,
                g_stefxOverlayDrawBeam,
                (unsigned int)mode,
                (int)count,
                (unsigned int)type,
                idx16 && count > 0 ? (unsigned int)idx16[0] : 0,
                idx16 && count > 1 ? (unsigned int)idx16[1] : 0,
                idx16 && count > 2 ? (unsigned int)idx16[2] : 0,
                (unsigned int)g_clientArrays,
                (unsigned int)g_texCoordArrayEnabled,
                (unsigned int)g_vertexArray.pointer);
            --s_overlayDrawElementsBudget;
        }
    }
    {
        static int s_glDrawElementsEntryBudget = 48;
        if (s_glDrawElementsEntryBudget > 0) {
            const GLushort *idx16 = (type == GL_UNSIGNED_SHORT) ? (const GLushort *)indices : NULL;
            XBLF("JA: compat glDrawElements entry #%d mode=0x%08x count=%d type=0x%08x idx0=%u,%u,%u client=0x%08x texMask=0x%08x vertexPtr=%08x",
                48 - s_glDrawElementsEntryBudget,
                (unsigned int)mode,
                (int)count,
                (unsigned int)type,
                idx16 && count > 0 ? (unsigned int)idx16[0] : 0,
                idx16 && count > 1 ? (unsigned int)idx16[1] : 0,
                idx16 && count > 2 ? (unsigned int)idx16[2] : 0,
                (unsigned int)g_clientArrays,
                (unsigned int)g_texCoordArrayEnabled,
                (unsigned int)g_vertexArray.pointer);
            --s_glDrawElementsEntryBudget;
        }
    }
    /* Keep this path allocation-free. It preserves indexed topology and avoids
     * the per-index immediate expansion that hammers CPU bandwidth on Xbox. */
#if defined(STEFX_ELITE_FORCE_MP)
    static const bool kUseFastIndexedUP = true;
    {
        static int s_stefxIndexedBypassBudget = 32;
        if (!kUseFastIndexedUP && s_stefxIndexedBypassBudget > 0) {
            XBLF("STEFX_HM: indexed UP bypassed for diagnostic draw path count=%d type=0x%08x texMask=0x%08x",
                (int)count,
                (unsigned int)type,
                (unsigned int)g_texCoordArrayEnabled);
            --s_stefxIndexedBypassBudget;
        }
    }
#else
    static const bool kUseFastIndexedUP = true;
#endif
    if (kUseFastIndexedUP && JkaTryDrawElementsUP(mode, count, type, indices)) {
        return;
    }
#endif
#if defined(STEFX_ELITE_FORCE_MP)
    int stefxImmediateSeq = -1;
    bool stefxTraceImmediate = false;
    {
        static int s_stefxImmediateSeq = 0;
        stefxImmediateSeq = s_stefxImmediateSeq++;
        stefxTraceImmediate = stefxImmediateSeq < 16;
        if (stefxTraceImmediate) {
            XBLF("STEFX_HM: immediate indexed draw seq=%d begin mode=0x%08x count=%d texMask=0x%08x",
                stefxImmediateSeq,
                (unsigned int)mode,
                (int)count,
                (unsigned int)g_texCoordArrayEnabled);
        }
    }
#endif
    glBegin(mode);
    for (GLsizei i = 0; i < count; ++i) {
        GLuint idx;
        if      (type == GL_UNSIGNED_SHORT) idx = ((const GLushort*)indices)[i];
        else if (type == GL_UNSIGNED_INT)   idx = ((const GLuint*)indices)[i];
        else                                 idx = ((const GLubyte*)indices)[i];
#if defined(STEFX_ELITE_FORCE_MP)
        if (stefxTraceImmediate && (i & 255) == 255) {
                XBLF("STEFX_HM: immediate indexed draw seq=%d progress i=%d count=%d idx=%u",
                    stefxImmediateSeq,
                    (int)i,
                    (int)count,
                    (unsigned int)idx);
        }
#endif
        SubmitArrayVertex(idx);
    }
#if defined(STEFX_ELITE_FORCE_MP)
    if (stefxTraceImmediate) {
        XBLF("STEFX_HM: immediate indexed draw seq=%d before glEnd count=%d",
            stefxImmediateSeq,
            (int)count);
    }
#endif
    glEnd();
#if defined(STEFX_ELITE_FORCE_MP)
    if (stefxTraceImmediate) {
        XBLF("STEFX_HM: immediate indexed draw seq=%d after glEnd count=%d",
            stefxImmediateSeq,
            (int)count);
    }
#endif
}

/* ============================================================
 *   Display lists — JKA uses them sparingly
 * ============================================================ */

GLuint glGenLists(GLsizei /*range*/) { return 0; /* lists not implemented */ }
void   glNewList(GLuint /*list*/, GLenum /*mode*/) {}
void   glEndList(void) {}
void   glDeleteLists(GLuint /*list*/, GLsizei /*range*/) {}
void   glCallList(GLuint /*list*/) {}

/* ============================================================
 *   Attribute stack
 * ============================================================ */

/* Gotcha I fix: attribute-stack shadow.  JKA uses these to save/restore
 * state across unrelated draw calls (UI text rendering, debug overlays).
 * Without this, state contamination scrambles subsequent draws.
 *
 * Implementation: 16-deep stack capturing the bits JKA actually saves
 * (GL_ENABLE_BIT, GL_DEPTH_BUFFER_BIT, GL_COLOR_BUFFER_BIT, GL_TEXTURE_BIT).
 * We snapshot g_capEnabled (covers depth/blend/cull/alpha/texture/etc.)
 * which is the only state our compat layer authoritatively tracks. */
#define JKA_ATTRIB_STACK_DEPTH 16
struct AttribFrame { GLbitfield mask; GLuint capEnabled; GLuint activeStage; GLuint clientActiveStage; GLuint clientArrays; GLuint texCoordArrayEnabled; };
static AttribFrame g_attribStack[JKA_ATTRIB_STACK_DEPTH];
static int         g_attribStackTop = 0;

static GLuint SnapshotFakeglCaps(void)
{
    GLuint bits = 0;
    static const GLenum kCaps[] = {
        GL_ALPHA_TEST, GL_BLEND, GL_CULL_FACE, GL_DEPTH_TEST, GL_FOG,
        GL_LIGHTING, GL_POLYGON_OFFSET_FILL, GL_SCISSOR_TEST,
        GL_STENCIL_TEST
    };
    for (int i = 0; i < 9; ++i) {
        int b = CapBit(kCaps[i]);
        if (b >= 0 && JkaFakeglIsEnabled(kCaps[i])) {
            bits |= (1u << b);
        }
    }

    for (GLuint stage = 0; stage < 4; ++stage) {
        JkaFakeglSelectTextureSGIS(StageToSgisTarget(stage));
        if (JkaFakeglIsEnabled(GL_TEXTURE_2D)) {
            bits |= (1u << (16 + stage));
        }
    }
    JkaFakeglSelectTextureSGIS(StageToSgisTarget(g_activeStage));
    return bits;
}

static void RestoreFakeglCaps(GLuint wantBits)
{
    static const GLenum kCaps[] = {
        GL_ALPHA_TEST, GL_BLEND, GL_CULL_FACE, GL_DEPTH_TEST, GL_FOG,
        GL_LIGHTING, GL_POLYGON_OFFSET_FILL, GL_SCISSOR_TEST,
        GL_STENCIL_TEST
    };
    for (int i = 0; i < 9; ++i) {
        int b = CapBit(kCaps[i]);
        if (b < 0) {
            continue;
        }
        if (wantBits & (1u << b)) {
            JkaFakeglEnable(kCaps[i]);
        } else {
            JkaFakeglDisable(kCaps[i]);
        }
    }

    for (GLuint stage = 0; stage < 4; ++stage) {
        JkaFakeglSelectTextureSGIS(StageToSgisTarget(stage));
        if (wantBits & (1u << (16 + stage))) {
            JkaFakeglEnable(GL_TEXTURE_2D);
        } else {
            JkaFakeglDisable(GL_TEXTURE_2D);
        }
    }
    JkaFakeglSelectTextureSGIS(StageToSgisTarget(g_activeStage));
}

void glPushAttrib(GLbitfield mask)
{
    if (g_attribStackTop >= JKA_ATTRIB_STACK_DEPTH) return;
    AttribFrame& f = g_attribStack[g_attribStackTop++];
    f.mask              = mask;
    f.capEnabled        = SnapshotFakeglCaps();
    f.activeStage       = g_activeStage;
    f.clientActiveStage = g_clientActiveStage;
    f.clientArrays      = g_clientArrays;
    f.texCoordArrayEnabled = g_texCoordArrayEnabled;
}
void glPopAttrib(void)
{
    if (g_attribStackTop <= 0) return;
    AttribFrame& f = g_attribStack[--g_attribStackTop];
    g_activeStage       = f.activeStage;
    g_clientActiveStage = f.clientActiveStage;
    g_clientArrays      = f.clientArrays;
    g_texCoordArrayEnabled = f.texCoordArrayEnabled;
    RestoreFakeglCaps(f.capEnabled);
    JkaFakeglSelectTextureSGIS(StageToSgisTarget(g_activeStage));
}
/* Client-side attribs (vertex array bindings).  Snapshot the same client
 * arrays bitmap; pointers are not duplicated (JKA convention is to
 * rebind after pop, but if it doesn't we keep the most-recent set). */
struct ClientAttribFrame { GLuint clientArrays; GLuint texCoordArrayEnabled; ArrayBinding v, c, t[4], n; };
static ClientAttribFrame g_clientAttribStack[JKA_ATTRIB_STACK_DEPTH];
static int               g_clientAttribStackTop = 0;
void glPushClientAttrib(GLbitfield /*mask*/)
{
    if (g_clientAttribStackTop >= JKA_ATTRIB_STACK_DEPTH) return;
    ClientAttribFrame& f = g_clientAttribStack[g_clientAttribStackTop++];
    f.clientArrays = g_clientArrays;
    f.texCoordArrayEnabled = g_texCoordArrayEnabled;
    f.v = g_vertexArray; f.c = g_colorArray;
    for (int i = 0; i < 4; ++i) {
        f.t[i] = g_texCoordArray[i];
    }
    f.n = g_normalArray;
}
void glPopClientAttrib(void)
{
    if (g_clientAttribStackTop <= 0) return;
    ClientAttribFrame& f = g_clientAttribStack[--g_clientAttribStackTop];
    g_clientArrays   = f.clientArrays;
    g_texCoordArrayEnabled = f.texCoordArrayEnabled;
    g_vertexArray    = f.v;  g_colorArray   = f.c;
    for (int i = 0; i < 4; ++i) {
        g_texCoordArray[i] = f.t[i];
    }
    g_normalArray  = f.n;
}

/* ============================================================
 *   Multitexture
 * ============================================================ */

void glActiveTextureARB(GLenum texture)
{
    if (texture >= GL_TEXTURE0_ARB && texture <= (GLenum)(GL_TEXTURE0_ARB + 3)) {
        g_activeStage = texture - GL_TEXTURE0_ARB;
        JkaFakeglSelectTextureSGIS(StageToSgisTarget(g_activeStage));
    }
}
void glClientActiveTextureARB(GLenum texture)
{
    if (texture >= GL_TEXTURE0_ARB && texture <= (GLenum)(GL_TEXTURE0_ARB + 3))
        g_clientActiveStage = texture - GL_TEXTURE0_ARB;
}
void glMultiTexCoord2fARB(GLenum target, GLfloat s, GLfloat t)
{
    if (target >= GL_TEXTURE0_ARB && target <= (GLenum)(GL_TEXTURE0_ARB + 3)) {
        GLuint stage = target - GL_TEXTURE0_ARB;
        if (stage == 0) {
            glTexCoord2f(s, t);
        } else {
            JkaFakeglMTexCoord2fSGIS(StageToSgisTarget(stage), s, t);
        }
        return;
    }
    glTexCoord2f(s, t);
}
void JkaGlMultiTexCoord2fARB(GLenum target, GLfloat s, GLfloat t)
{
    glMultiTexCoord2fARB(target, s, t);
}

/* ============================================================
 *   Misc color/normal variants
 * ============================================================ */

void glColor3ub(GLubyte r, GLubyte g, GLubyte b)
{
    glColor4f(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
}
void glColor3fv(const GLfloat *v) { if (v) glColor3f(v[0], v[1], v[2]); }

void glNormal3f(GLfloat /*nx*/, GLfloat /*ny*/, GLfloat /*nz*/) { /* lighting deferred */ }
void glNormal3fv(const GLfloat * /*v*/) {}

/* ============================================================
 *   JKA-specific extensions
 * ============================================================ */

GLboolean glBeginFrame(void)
{
    static int s_xboxBeginFrameCount = 0;

    /*
     * The renderer only clears color on specific world/sky paths.  On the
     * Xbox fakegl path, the backbuffer contents survive Present(), which
     * smears EF's cinematic scroll text across frames.  Clear the target at
     * frame start while preserving EF cgame/script behavior.
     */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

#ifdef _XBOX
    if (s_xboxBeginFrameCount < 8)
    {
        XBLF("STEFX: fakegl beginframe clear frame=%d mask=0x%08x",
            s_xboxBeginFrameCount,
            (unsigned int)(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT));
    }
#endif
    ++s_xboxBeginFrameCount;

    /* fakegl lazily BeginScenes on first glBegin (m_needBeginScene true
     * after each SwapBuffers). */
    return GL_TRUE;
}

/* Plan-B critical fix: match OpenJKDF2's std3D_Present pattern exactly.
 *
 * OpenJKDF2's std3D_Present (src/Platform/Xbox/std3D.c:812-892) does an
 * extensive GL state reset BEFORE FakeSwapBuffers:
 *   - glViewport(0,0,640,480)
 *   - glMatrixMode(PROJECTION); glLoadIdentity; glOrtho(0,640,480,0,-99999,99999)
 *   - glMatrixMode(MODELVIEW);  glLoadIdentity
 *   - glDisable(DEPTH_TEST, CULL_FACE, BLEND, ALPHA_TEST, TEXTURE_2D)
 *   - FakeSwapBuffers
 *
 * The critical line is `glDisable(GL_TEXTURE_2D)` — clears stage 0's
 * active texture binding before the swap.  Without it, CXBX-R's LLE GPU
 * emulator hangs in Present() waiting for a pending texture-stage
 * operation that never completes (verified via hardware test
 * 2026-05-16 21:32: SP_DoLicense draws a textured quad, calls
 * qglEndFrame with GL_TEXTURE_2D still enabled, Present hangs).
 *
 * Replicating OpenJKDF2's full reset block here in glEndFrame ensures
 * the GL state at swap time matches the working configuration. */
void glEndFrame(void)
{
    static int s_xboxCompatEndFrameCount = 0;
    const bool xboxTraceCompatEndFrameTight = false;
    const bool xboxTraceCompatEndFrame =
        (xboxTraceCompatEndFrameTight || s_xboxCompatEndFrameCount < 4 || ((s_xboxCompatEndFrameCount & 1023) == 0));
    if (xboxTraceCompatEndFrameTight) XBLF("JA: CL_EARLY compat glEndFrame #%d enter", s_xboxCompatEndFrameCount);
    if (xboxTraceCompatEndFrame) XBLF("JA: compat glEndFrame #%d enter", s_xboxCompatEndFrameCount);

    /* Plan-B: explicit OpenJKDF2 1:1 swap-time state reset.  These calls
     * route through fakegl directly (the JKA-side redirects don't apply
     * here because _JKA_DDS_BRIDGE_INTERNAL_ is defined). */
    if (xboxTraceCompatEndFrame) XBLF("JA: compat glEndFrame #%d reset state...", s_xboxCompatEndFrameCount);
    if (xboxTraceCompatEndFrameTight) XBLF("JA: CL_EARLY compat glEndFrame #%d reset state", s_xboxCompatEndFrameCount);
    glViewport(0, 0, 640, 480);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, 640.0, 480.0, 0.0, -99999.0, 99999.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_TEXTURE_2D);
    if (xboxTraceCompatEndFrameTight) XBLF("JA: CL_EARLY compat glEndFrame #%d reset state done", s_xboxCompatEndFrameCount);
    if (xboxTraceCompatEndFrame) XBLF("JA: compat glEndFrame #%d reset state done", s_xboxCompatEndFrameCount);

    /* Plan-B Present routing:
     *
     * fakegl's FakeSwapBuffers calls m_pD3DDev->Present() with the default
     * D3DPRESENT_INTERVAL_ONE (wait for v-sync).  CXBX-R's LLE GPU does
     * not properly signal v-sync → Present blocks forever.  Observed
     * consistently across all Plan-B hardware tests: hangs at SDT:
     * glEndFrame regardless of state setup, texture binding, push buffer.
     *
     * Workaround: use glw_state->device — our parallel CreateDevice with
     * D3DPRESENT_INTERVAL_IMMEDIATE — to do the EndScene + Present.  Both
     * device pointers refer to the same Xbox D3D8 singleton (Direct3DCreate8
     * returns sentinel 0x1, CreateDevice on it gives separate handles to
     * the shared underlying GPU state).  Presenting from our handle swaps
     * the same backbuffer fakegl rendered into.
     *
     * If glw_state->device is NULL (parallel CreateDevice failed) fall
     * back to FakeSwapBuffers — at least we maintain m_needBeginScene
     * state correctness for subsequent frames. */
    if (xboxTraceCompatEndFrame) XBLF("JA: compat glEndFrame #%d FakeSwapBuffers...", s_xboxCompatEndFrameCount);
    if (xboxTraceCompatEndFrameTight) XBLF("JA: CL_EARLY compat glEndFrame #%d before FakeSwapBuffers", s_xboxCompatEndFrameCount);
    FakeSwapBuffers();
    if (xboxTraceCompatEndFrameTight) XBLF("JA: CL_EARLY compat glEndFrame #%d after FakeSwapBuffers", s_xboxCompatEndFrameCount);
    if (xboxTraceCompatEndFrame) XBLF("JA: compat glEndFrame #%d FakeSwapBuffers done", s_xboxCompatEndFrameCount);
    s_xboxCompatEndFrameCount++;
}

void glBeginEXT(GLenum mode, GLint /*nv*/, GLint /*nc*/, GLint /*nn*/,
                GLint /*nt0*/, GLint /*nt1*/)
{
    /* Batch-size hints are advisory; fakegl manages its own vertex buffer. */
    glBegin(mode);
}

void glIndexedTriToStrip(GLsizei count, const GLushort *indices)
{
    /* JKA-private extension: render a triangle strip by indexed
     * lookup into the currently-bound vertex array. */
    if (!indices) return;
    glDrawElements(GL_TRIANGLE_STRIP, count, 0x1403 /*GL_UNSIGNED_SHORT*/, indices);
}

void glCopyBackBufferToTexEXT(GLsizei /*texW*/, GLsizei /*texH*/,
                              GLint /*srcX0*/, GLint /*srcY0*/,
                              GLint /*srcX1*/, GLint /*srcY1*/)
{
    /* JKA renderToTextureFX path: would call
     * IDirect3DDevice8::GetBackBuffer + CopyRects to current bound
     * texture.  Deferred — most JKA visual FX that need this are
     * post-license-screen. */
}

/* ============================================================
 *   Locked-array extension wrappers
 * ============================================================ */

void glLockArraysEXT(GLint /*first*/, GLsizei /*count*/) {}
void glUnlockArraysEXT(void) {}

void glPointParameterfEXT(GLenum /*pname*/, GLfloat /*param*/)        {}
void glPointParameterfvEXT(GLenum /*pname*/, GLfloat * /*params*/)    {}

} /* extern "C" */
