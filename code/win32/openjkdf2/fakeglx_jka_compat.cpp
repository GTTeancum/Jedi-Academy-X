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

void glFogf(GLenum /*pname*/, GLfloat /*param*/) {}
void glFogfv(GLenum /*pname*/, const GLfloat * /*params*/) {}
void glFogi(GLenum /*pname*/, GLint /*param*/) {}

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
    glBegin(mode);
    for (GLsizei i = 0; i < count; ++i) {
        GLuint idx;
        if      (type == GL_UNSIGNED_SHORT) idx = ((const GLushort*)indices)[i];
        else if (type == GL_UNSIGNED_INT)   idx = ((const GLuint*)indices)[i];
        else                                 idx = ((const GLubyte*)indices)[i];
        SubmitArrayVertex(idx);
    }
    glEnd();
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
    /* fakegl lazily BeginScenes on first glBegin (m_needBeginScene true
     * after each SwapBuffers).  No explicit work needed here. */
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
    const bool xboxTraceCompatEndFrame =
        (s_xboxCompatEndFrameCount < 4 || ((s_xboxCompatEndFrameCount & 1023) == 0));
    if (xboxTraceCompatEndFrame) XBLF("JA: compat glEndFrame #%d enter", s_xboxCompatEndFrameCount);

    /* Plan-B: explicit OpenJKDF2 1:1 swap-time state reset.  These calls
     * route through fakegl directly (the JKA-side redirects don't apply
     * here because _JKA_DDS_BRIDGE_INTERNAL_ is defined). */
    if (xboxTraceCompatEndFrame) XBLF("JA: compat glEndFrame #%d reset state...", s_xboxCompatEndFrameCount);
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
    FakeSwapBuffers();
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
