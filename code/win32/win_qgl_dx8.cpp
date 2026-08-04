
/*
 * UNPUBLISHED -- Rights  reserved  under  the  copyright  laws  of the 
 * United States.  Use  of a copyright notice is precautionary only and 
 * does not imply publication or disclosure.                            
 *                                                                      
 * THIS DOCUMENTATION CONTAINS CONFIDENTIAL AND PROPRIETARY INFORMATION 
 * OF    VICARIOUS   VISIONS,  INC.    ANY  DUPLICATION,  MODIFICATION, 
 * DISTRIBUTION, OR DISCLOSURE IS STRICTLY PROHIBITED WITHOUT THE PRIOR 
 * EXPRESS WRITTEN PERMISSION OF VICARIOUS VISIONS, INC.
 */

// leave this as first line for PCH reasons...
//
#include "../server/exe_headers.h"


/*
** QGL_WIN.C
**
** This file implements the operating system binding of GL to QGL function
** pointers.  When doing a port of Quake3 you must implement the following
** two functions:
**
** QGL_Init() - loads libraries, assigns function pointers, etc.
** QGL_Shutdown() - unloads libraries, NULLs function pointers
*/
#include <float.h>
#include "../renderer/tr_local.h"
#include "glw_win_dx8.h"
#include "win_local.h"

#include "xbox_texture_man.h"

#ifdef _XBOX
#include <xgraphics.h>
//#include "win_flareeffect.h"
#include "win_lighteffects.h"
#include "win_highdynamicrange.h"
#include "win_stencilshadow.h"
#include "xb_log.h"
extern "C" volatile unsigned int g_SPXBPhaseLast;
extern "C" volatile unsigned int g_SPXBFakeGLPrimitiveCalls;
extern "C" volatile unsigned int g_SPXBFakeGLPrimitiveVerts;
extern "C" volatile unsigned int g_SPXBNativeUpCalls;
extern "C" volatile unsigned int g_SPXBNativeUpBytes;
extern "C" volatile unsigned int g_SPXBNativePushCalls;
extern "C" volatile unsigned int g_SPXBNativePushBytes;
extern "C" volatile unsigned int g_SPXBNativePushReuse;
extern "C" volatile unsigned int g_SPXBNativePushFallbacks;
extern "C" volatile unsigned int g_SPXBNativeRingCalls;
extern "C" volatile unsigned int g_SPXBNativeRingBytes;
extern "C" volatile unsigned int g_SPXBNativeRingWraps;
extern "C" volatile unsigned int g_SPXBNativeRingFallbacks;
extern "C" volatile unsigned int g_SPXBNativeDrawMode;
extern "C" volatile unsigned int g_SPXBNativeDrawCount;
extern "C" volatile unsigned int g_SPXBNativeDrawSourceVertices;
extern "C" volatile unsigned int g_SPXBNativeDrawMaxIndex;
extern "C" volatile unsigned int g_SPXBNativeDrawStride;
extern "C" volatile unsigned int g_SPXBNativeDrawIndicesPtr;
extern "C" volatile unsigned int g_SPXBNativeDrawVerticesPtr;
extern "C" volatile unsigned int g_SPXBNativeDrawPath;
extern "C" volatile unsigned int g_SPXBNativeDrawShader;
extern "C" volatile unsigned int g_SPXBNativeDrawVertexOffset;
extern "C" volatile unsigned int g_SPXBNativeDrawIndexOffset;
extern "C" volatile unsigned int g_SPXBNativeDrawVertexBytes;
extern "C" volatile unsigned int g_SPXBNativeDrawIndexBytes;
extern "C" volatile unsigned int g_SPXBNativeDrawLockFlags;
extern "C" volatile unsigned int g_SPXBNativeDrawMinIndex;

#ifndef FINAL_BUILD
#include <d3d8perf.h>
#endif

#endif

// Global texture allocator (we only use one in SP):
//SwappingTextureAllocator	gTextures;
StaticTextureAllocator		gTextures;

#include <vector>

extern void Z_SetNewDeleteTemporary(bool);

#define GLW_USE_TRI_STRIPS 0

enum
{
	STEFX_MAX_NATIVE_VERTEX_DWORDS = 11,
	STEFX_MAX_IMMEDIATE_VERTICES = 8192
};

static DWORD s_immediateVertices[
	STEFX_MAX_IMMEDIATE_VERTICES * STEFX_MAX_NATIVE_VERTEX_DWORDS];
static bool s_immediateUsesUP = false;
static bool s_immediateHasNormal = false;
static bool s_immediateHasTexCoord[GLW_MAX_TEXTURE_STAGES] = { false, false };
static float s_immediateNormal[3] = { 0.0f, 0.0f, 1.0f };
static float s_immediateTexCoord[GLW_MAX_TEXTURE_STAGES][2] =
{
	{ 0.0f, 0.0f },
	{ 0.0f, 0.0f }
};

#ifdef _XBOX
enum
{
	STEFX_NATIVE_RING_VERTEX_BYTES = 256 * 1024,
	STEFX_NATIVE_RING_INDEX_BYTES = 128 * 1024
};

static IDirect3DVertexBuffer8 *s_nativeVertexRing = NULL;
static IDirect3DIndexBuffer8 *s_nativeIndexRing = NULL;
static DWORD s_nativeVertexRingOffset = 0;
static DWORD s_nativeIndexRingOffset = 0;
static unsigned int s_nativePushRejectReason = 0;

static bool SubmitNativeVerticesRing(D3DPRIMITIVETYPE primitiveMode,
	const DWORD *vertices, DWORD vertexCount, DWORD strideBytes);
static bool SubmitNativeTriangleListPush(const DWORD *vertices,
	DWORD vertexCount, DWORD strideBytes);

#define GLW_MAX_DRAW_PACKET_SIZE 2040
#else
#define GLW_MAX_DRAW_PACKET_SIZE (SHADER_MAX_VERTEXES*12)
#endif

#define MEMORY_PROFILE 1

int texMemSize = 0;

#if MEMORY_PROFILE
 
static int getTexMemSize(IDirect3DTexture8* mipmap)
{
	int levels = mipmap->GetLevelCount();
	int size = 0;
	while (levels--)
	{ 
		D3DSURFACE_DESC desc;
		mipmap->GetLevelDesc(levels, &desc);
		size += desc.Size;
	}
	return size;
}
#endif

void QGL_EnableLogging( qboolean enable );

void ( * qglAccum )(GLenum op, GLfloat value);
void ( * qglAlphaFunc )(GLenum func, GLclampf ref);
GLboolean ( * qglAreTexturesResident )(GLsizei n, const GLuint *textures, GLboolean *residences);
void ( * qglArrayElement )(GLint i);
void ( * qglBegin )(GLenum mode);
void ( * qglBeginEXT )(GLenum mode, GLint verts, GLint colors, GLint normals, GLint tex0, GLint tex1);//, GLint tex2, GLint tex3);
GLboolean ( * qglBeginFrame )(void);
void ( * qglBeginShadow )(void);
void ( * qglBindTexture )(GLenum target, GLuint texture);
void ( * qglBitmap )(GLsizei width, GLsizei height, GLfloat xorig, GLfloat yorig, GLfloat xmove, GLfloat ymove, const GLubyte *bitmap);
void ( * qglBlendFunc )(GLenum sfactor, GLenum dfactor);
void ( * qglCallList )(GLuint lnum);
void ( * qglCallLists )(GLsizei n, GLenum type, const GLvoid *lists);
void ( * qglClear )(GLbitfield mask);
void ( * qglClearAccum )(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
void ( * qglClearColor )(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
void ( * qglClearDepth )(GLclampd depth);
void ( * qglClearIndex )(GLfloat c);
void ( * qglClearStencil )(GLint s);
void ( * qglClipPlane )(GLenum plane, const GLdouble *equation);
void ( * qglColor3b )(GLbyte red, GLbyte green, GLbyte blue);
void ( * qglColor3bv )(const GLbyte *v);
void ( * qglColor3d )(GLdouble red, GLdouble green, GLdouble blue);
void ( * qglColor3dv )(const GLdouble *v);
void ( * qglColor3f )(GLfloat red, GLfloat green, GLfloat blue);
void ( * qglColor3fv )(const GLfloat *v);
void ( * qglColor3i )(GLint red, GLint green, GLint blue);
void ( * qglColor3iv )(const GLint *v);
void ( * qglColor3s )(GLshort red, GLshort green, GLshort blue);
void ( * qglColor3sv )(const GLshort *v);
void ( * qglColor3ub )(GLubyte red, GLubyte green, GLubyte blue);
void ( * qglColor3ubv )(const GLubyte *v);
void ( * qglColor3ui )(GLuint red, GLuint green, GLuint blue);
void ( * qglColor3uiv )(const GLuint *v);
void ( * qglColor3us )(GLushort red, GLushort green, GLushort blue);
void ( * qglColor3usv )(const GLushort *v);
void ( * qglColor4b )(GLbyte red, GLbyte green, GLbyte blue, GLbyte alpha);
void ( * qglColor4bv )(const GLbyte *v);
void ( * qglColor4d )(GLdouble red, GLdouble green, GLdouble blue, GLdouble alpha);
void ( * qglColor4dv )(const GLdouble *v);
void ( * qglColor4f )(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
void ( * qglColor4fv )(const GLfloat *v);
void ( * qglColor4i )(GLint red, GLint green, GLint blue, GLint alpha);
void ( * qglColor4iv )(const GLint *v);
void ( * qglColor4s )(GLshort red, GLshort green, GLshort blue, GLshort alpha);
void ( * qglColor4sv )(const GLshort *v);
void ( * qglColor4ub )(GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha);
void ( * qglColor4ubv )(const GLubyte *v);
void ( * qglColor4ui )(GLuint red, GLuint green, GLuint blue, GLuint alpha);
void ( * qglColor4uiv )(const GLuint *v);
void ( * qglColor4us )(GLushort red, GLushort green, GLushort blue, GLushort alpha);
void ( * qglColor4usv )(const GLushort *v);
void ( * qglColorMask )(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);
void ( * qglColorMaterial )(GLenum face, GLenum mode);
void ( * qglColorPointer )(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void ( * qglCopyPixels )(GLint x, GLint y, GLsizei width, GLsizei height, GLenum type);
void ( * qglCopyTexImage1D )(GLenum target, GLint level, GLenum internalFormat, GLint x, GLint y, GLsizei width, GLint border);
void ( * qglCopyTexImage2D )(GLenum target, GLint level, GLenum internalFormat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border);
void ( * qglCopyTexSubImage1D )(GLenum target, GLint level, GLint xoffset, GLint x, GLint y, GLsizei width);
void ( * qglCopyTexSubImage2D )(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height);
void ( * qglCullFace )(GLenum mode);
void ( * qglDeleteLists )(GLuint lnum, GLsizei range);
void ( * qglDeleteTextures )(GLsizei n, const GLuint *textures);
void ( * qglDepthFunc )(GLenum func);
void ( * qglDepthMask )(GLboolean flag);
void ( * qglDepthRange )(GLclampd zNear, GLclampd zFar);
void ( * qglDisable )(GLenum cap);
void ( * qglDisableClientState )(GLenum array);
void ( * qglDrawArrays )(GLenum mode, GLint first, GLsizei count);
void ( * qglDrawBuffer )(GLenum mode);
void ( * qglDrawElements )(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices);
void ( * qglDrawPixels )(GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels);
void ( * qglEdgeFlag )(GLboolean flag);
void ( * qglEdgeFlagPointer )(GLsizei stride, const GLvoid *pointer);
void ( * qglEdgeFlagv )(const GLboolean *flag);
void ( * qglEnable )(GLenum cap);
void ( * qglEnableClientState )(GLenum array);
void ( * qglEnd )(void);
void ( * qglEndFrame )(void);
void ( * qglEndShadow )(void);
void ( * qglEndList )(void);
void ( * qglEvalCoord1d )(GLdouble u);
void ( * qglEvalCoord1dv )(const GLdouble *u);
void ( * qglEvalCoord1f )(GLfloat u);
void ( * qglEvalCoord1fv )(const GLfloat *u);
void ( * qglEvalCoord2d )(GLdouble u, GLdouble v);
void ( * qglEvalCoord2dv )(const GLdouble *u);
void ( * qglEvalCoord2f )(GLfloat u, GLfloat v);
void ( * qglEvalCoord2fv )(const GLfloat *u);
void ( * qglEvalMesh1 )(GLenum mode, GLint i1, GLint i2);
void ( * qglEvalMesh2 )(GLenum mode, GLint i1, GLint i2, GLint j1, GLint j2);
void ( * qglEvalPoint1 )(GLint i);
void ( * qglEvalPoint2 )(GLint i, GLint j);
void ( * qglFeedbackBuffer )(GLsizei size, GLenum type, GLfloat *buffer);
void ( * qglFinish )(void);
void ( * qglFlush )(void);
void ( * qglFlushShadow )(void);
void ( * qglFogf )(GLenum pname, GLfloat param);
void ( * qglFogfv )(GLenum pname, const GLfloat *params);
void ( * qglFogi )(GLenum pname, GLint param);
void ( * qglFogiv )(GLenum pname, const GLint *params);
void ( * qglFrontFace )(GLenum mode);
void ( * qglFrustum )(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar);
GLuint ( * qglGenLists )(GLsizei range);
void ( * qglGenTextures )(GLsizei n, GLuint *textures);
void ( * qglGetBooleanv )(GLenum pname, GLboolean *params);
void ( * qglGetClipPlane )(GLenum plane, GLdouble *equation);
void ( * qglGetDoublev )(GLenum pname, GLdouble *params);
GLenum ( * qglGetError )(void);
void ( * qglGetFloatv )(GLenum pname, GLfloat *params);
void ( * qglGetIntegerv )(GLenum pname, GLint *params);
void ( * qglGetLightfv )(GLenum light, GLenum pname, GLfloat *params);
void ( * qglGetLightiv )(GLenum light, GLenum pname, GLint *params);
void ( * qglGetMapdv )(GLenum target, GLenum query, GLdouble *v);
void ( * qglGetMapfv )(GLenum target, GLenum query, GLfloat *v);
void ( * qglGetMapiv )(GLenum target, GLenum query, GLint *v);
void ( * qglGetMaterialfv )(GLenum face, GLenum pname, GLfloat *params);
void ( * qglGetMaterialiv )(GLenum face, GLenum pname, GLint *params);
void ( * qglGetPixelMapfv )(GLenum gmap, GLfloat *values);
void ( * qglGetPixelMapuiv )(GLenum gmap, GLuint *values);
void ( * qglGetPixelMapusv )(GLenum gmap, GLushort *values);
void ( * qglGetPointerv )(GLenum pname, GLvoid* *params);
void ( * qglGetPolygonStipple )(GLubyte *mask);
const GLubyte * ( * qglGetString )(GLenum name);
void ( * qglGetTexEnvfv )(GLenum target, GLenum pname, GLfloat *params);
void ( * qglGetTexEnviv )(GLenum target, GLenum pname, GLint *params);
void ( * qglGetTexGendv )(GLenum coord, GLenum pname, GLdouble *params);
void ( * qglGetTexGenfv )(GLenum coord, GLenum pname, GLfloat *params);
void ( * qglGetTexGeniv )(GLenum coord, GLenum pname, GLint *params);
void ( * qglGetTexImage )(GLenum target, GLint level, GLenum format, GLenum type, GLvoid *pixels);
void ( * qglGetTexLevelParameterfv )(GLenum target, GLint level, GLenum pname, GLfloat *params);
void ( * qglGetTexLevelParameteriv )(GLenum target, GLint level, GLenum pname, GLint *params);
void ( * qglGetTexParameterfv )(GLenum target, GLenum pname, GLfloat *params);
void ( * qglGetTexParameteriv )(GLenum target, GLenum pname, GLint *params);
void ( * qglHint )(GLenum target, GLenum mode);
void ( * qglIndexedTriToStrip )(GLsizei count, const GLushort *indices);
void ( * qglIndexMask )(GLuint mask);
void ( * qglIndexPointer )(GLenum type, GLsizei stride, const GLvoid *pointer);
void ( * qglIndexd )(GLdouble c);
void ( * qglIndexdv )(const GLdouble *c);
void ( * qglIndexf )(GLfloat c);
void ( * qglIndexfv )(const GLfloat *c);
void ( * qglIndexi )(GLint c);
void ( * qglIndexiv )(const GLint *c);
void ( * qglIndexs )(GLshort c);
void ( * qglIndexsv )(const GLshort *c);
void ( * qglIndexub )(GLubyte c);
void ( * qglIndexubv )(const GLubyte *c);
void ( * qglInitNames )(void);
void ( * qglInterleavedArrays )(GLenum format, GLsizei stride, const GLvoid *pointer);
GLboolean ( * qglIsEnabled )(GLenum cap);
GLboolean ( * qglIsList )(GLuint lnum);
GLboolean ( * qglIsTexture )(GLuint texture);
void ( * qglLightModelf )(GLenum pname, GLfloat param);
void ( * qglLightModelfv )(GLenum pname, const GLfloat *params);
void ( * qglLightModeli )(GLenum pname, GLint param);
void ( * qglLightModeliv )(GLenum pname, const GLint *params);
void ( * qglLightf )(GLenum light, GLenum pname, GLfloat param);
void ( * qglLightfv )(GLenum light, GLenum pname, const GLfloat *params);
void ( * qglLighti )(GLenum light, GLenum pname, GLint param);
void ( * qglLightiv )(GLenum light, GLenum pname, const GLint *params);
void ( * qglLineStipple )(GLint factor, GLushort pattern);
void ( * qglLineWidth )(GLfloat width);
void ( * qglListBase )(GLuint base);
void ( * qglLoadIdentity )(void);
void ( * qglLoadMatrixd )(const GLdouble *m);
void ( * qglLoadMatrixf )(const GLfloat *m);
void ( * qglLoadName )(GLuint name);
void ( * qglLogicOp )(GLenum opcode);
void ( * qglMap1d )(GLenum target, GLdouble u1, GLdouble u2, GLint stride, GLint order, const GLdouble *points);
void ( * qglMap1f )(GLenum target, GLfloat u1, GLfloat u2, GLint stride, GLint order, const GLfloat *points);
void ( * qglMap2d )(GLenum target, GLdouble u1, GLdouble u2, GLint ustride, GLint uorder, GLdouble v1, GLdouble v2, GLint vstride, GLint vorder, const GLdouble *points);
void ( * qglMap2f )(GLenum target, GLfloat u1, GLfloat u2, GLint ustride, GLint uorder, GLfloat v1, GLfloat v2, GLint vstride, GLint vorder, const GLfloat *points);
void ( * qglMapGrid1d )(GLint un, GLdouble u1, GLdouble u2);
void ( * qglMapGrid1f )(GLint un, GLfloat u1, GLfloat u2);
void ( * qglMapGrid2d )(GLint un, GLdouble u1, GLdouble u2, GLint vn, GLdouble v1, GLdouble v2);
void ( * qglMapGrid2f )(GLint un, GLfloat u1, GLfloat u2, GLint vn, GLfloat v1, GLfloat v2);
void ( * qglMaterialf )(GLenum face, GLenum pname, GLfloat param);
void ( * qglMaterialfv )(GLenum face, GLenum pname, const GLfloat *params);
void ( * qglMateriali )(GLenum face, GLenum pname, GLint param);
void ( * qglMaterialiv )(GLenum face, GLenum pname, const GLint *params);
void ( * qglMatrixMode )(GLenum mode);
void ( * qglMultMatrixd )(const GLdouble *m);
void ( * qglMultMatrixf )(const GLfloat *m);
void ( * qglNewList )(GLuint lnum, GLenum mode);
void ( * qglNormal3b )(GLbyte nx, GLbyte ny, GLbyte nz);
void ( * qglNormal3bv )(const GLbyte *v);
void ( * qglNormal3d )(GLdouble nx, GLdouble ny, GLdouble nz);
void ( * qglNormal3dv )(const GLdouble *v);
void ( * qglNormal3f )(GLfloat nx, GLfloat ny, GLfloat nz);
void ( * qglNormal3fv )(const GLfloat *v);
void ( * qglNormal3i )(GLint nx, GLint ny, GLint nz);
void ( * qglNormal3iv )(const GLint *v);
void ( * qglNormal3s )(GLshort nx, GLshort ny, GLshort nz);
void ( * qglNormal3sv )(const GLshort *v);
void ( * qglNormalPointer )(GLenum type, GLsizei stride, const GLvoid *pointer);
void ( * qglOrtho )(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar);
void ( * qglPassThrough )(GLfloat token);
void ( * qglPixelMapfv )(GLenum gmap, GLsizei mapsize, const GLfloat *values);
void ( * qglPixelMapuiv )(GLenum gmap, GLsizei mapsize, const GLuint *values);
void ( * qglPixelMapusv )(GLenum gmap, GLsizei mapsize, const GLushort *values);
void ( * qglPixelStoref )(GLenum pname, GLfloat param);
void ( * qglPixelStorei )(GLenum pname, GLint param);
void ( * qglPixelTransferf )(GLenum pname, GLfloat param);
void ( * qglPixelTransferi )(GLenum pname, GLint param);
void ( * qglPixelZoom )(GLfloat xfactor, GLfloat yfactor);
void ( * qglPointSize )(GLfloat size);
void ( * qglPolygonMode )(GLenum face, GLenum mode);
void ( * qglPolygonOffset )(GLfloat factor, GLfloat units);
void ( * qglPolygonStipple )(const GLubyte *mask);
void ( * qglPopAttrib )(void);
void ( * qglPopClientAttrib )(void);
void ( * qglPopMatrix )(void);
void ( * qglPopName )(void);
void ( * qglPrioritizeTextures )(GLsizei n, const GLuint *textures, const GLclampf *priorities);
void ( * qglPushAttrib )(GLbitfield mask);
void ( * qglPushClientAttrib )(GLbitfield mask);
void ( * qglPushMatrix )(void);
void ( * qglPushName )(GLuint name);
void ( * qglRasterPos2d )(GLdouble x, GLdouble y);
void ( * qglRasterPos2dv )(const GLdouble *v);
void ( * qglRasterPos2f )(GLfloat x, GLfloat y);
void ( * qglRasterPos2fv )(const GLfloat *v);
void ( * qglRasterPos2i )(GLint x, GLint y);
void ( * qglRasterPos2iv )(const GLint *v);
void ( * qglRasterPos2s )(GLshort x, GLshort y);
void ( * qglRasterPos2sv )(const GLshort *v);
void ( * qglRasterPos3d )(GLdouble x, GLdouble y, GLdouble z);
void ( * qglRasterPos3dv )(const GLdouble *v);
void ( * qglRasterPos3f )(GLfloat x, GLfloat y, GLfloat z);
void ( * qglRasterPos3fv )(const GLfloat *v);
void ( * qglRasterPos3i )(GLint x, GLint y, GLint z);
void ( * qglRasterPos3iv )(const GLint *v);
void ( * qglRasterPos3s )(GLshort x, GLshort y, GLshort z);
void ( * qglRasterPos3sv )(const GLshort *v);
void ( * qglRasterPos4d )(GLdouble x, GLdouble y, GLdouble z, GLdouble w);
void ( * qglRasterPos4dv )(const GLdouble *v);
void ( * qglRasterPos4f )(GLfloat x, GLfloat y, GLfloat z, GLfloat w);
void ( * qglRasterPos4fv )(const GLfloat *v);
void ( * qglRasterPos4i )(GLint x, GLint y, GLint z, GLint w);
void ( * qglRasterPos4iv )(const GLint *v);
void ( * qglRasterPos4s )(GLshort x, GLshort y, GLshort z, GLshort w);
void ( * qglRasterPos4sv )(const GLshort *v);
void ( * qglReadBuffer )(GLenum mode);
//void ( * qglReadPixels )(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLsizei twidth, GLsizei theight, GLvoid *pixels);
void ( * qglReadPixels )(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels);
void ( * qglCopyBackBufferToTexEXT ) (float width, float height, float u1, float v1, float u2, float v2);
void ( * qglCopyBackBufferToTex ) (void);
void ( * qglRectd )(GLdouble x1, GLdouble y1, GLdouble x2, GLdouble y2);
void ( * qglRectdv )(const GLdouble *v1, const GLdouble *v2);
void ( * qglRectf )(GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2);
void ( * qglRectfv )(const GLfloat *v1, const GLfloat *v2);
void ( * qglRecti )(GLint x1, GLint y1, GLint x2, GLint y2);
void ( * qglRectiv )(const GLint *v1, const GLint *v2);
void ( * qglRects )(GLshort x1, GLshort y1, GLshort x2, GLshort y2);
void ( * qglRectsv )(const GLshort *v1, const GLshort *v2);
GLint ( * qglRenderMode )(GLenum mode);
void ( * qglRotated )(GLdouble angle, GLdouble x, GLdouble y, GLdouble z);
void ( * qglRotatef )(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
void ( * qglScaled )(GLdouble x, GLdouble y, GLdouble z);
void ( * qglScalef )(GLfloat x, GLfloat y, GLfloat z);
void ( * qglScissor )(GLint x, GLint y, GLsizei width, GLsizei height);
void ( * qglSelectBuffer )(GLsizei size, GLuint *buffer);
void ( * qglShadeModel )(GLenum mode);
void ( * qglStencilFunc )(GLenum func, GLint ref, GLuint mask);
void ( * qglStencilMask )(GLuint mask);
void ( * qglStencilOp )(GLenum fail, GLenum zfail, GLenum zpass);
void ( * qglTexCoord1d )(GLdouble s);
void ( * qglTexCoord1dv )(const GLdouble *v);
void ( * qglTexCoord1f )(GLfloat s);
void ( * qglTexCoord1fv )(const GLfloat *v);
void ( * qglTexCoord1i )(GLint s);
void ( * qglTexCoord1iv )(const GLint *v);
void ( * qglTexCoord1s )(GLshort s);
void ( * qglTexCoord1sv )(const GLshort *v);
void ( * qglTexCoord2d )(GLdouble s, GLdouble t);
void ( * qglTexCoord2dv )(const GLdouble *v);
void ( * qglTexCoord2f )(GLfloat s, GLfloat t);
void ( * qglTexCoord2fv )(const GLfloat *v);
void ( * qglTexCoord2i )(GLint s, GLint t);
void ( * qglTexCoord2iv )(const GLint *v);
void ( * qglTexCoord2s )(GLshort s, GLshort t);
void ( * qglTexCoord2sv )(const GLshort *v);
void ( * qglTexCoord3d )(GLdouble s, GLdouble t, GLdouble r);
void ( * qglTexCoord3dv )(const GLdouble *v);
void ( * qglTexCoord3f )(GLfloat s, GLfloat t, GLfloat r);
void ( * qglTexCoord3fv )(const GLfloat *v);
void ( * qglTexCoord3i )(GLint s, GLint t, GLint r);
void ( * qglTexCoord3iv )(const GLint *v);
void ( * qglTexCoord3s )(GLshort s, GLshort t, GLshort r);
void ( * qglTexCoord3sv )(const GLshort *v);
void ( * qglTexCoord4d )(GLdouble s, GLdouble t, GLdouble r, GLdouble q);
void ( * qglTexCoord4dv )(const GLdouble *v);
void ( * qglTexCoord4f )(GLfloat s, GLfloat t, GLfloat r, GLfloat q);
void ( * qglTexCoord4fv )(const GLfloat *v);
void ( * qglTexCoord4i )(GLint s, GLint t, GLint r, GLint q);
void ( * qglTexCoord4iv )(const GLint *v);
void ( * qglTexCoord4s )(GLshort s, GLshort t, GLshort r, GLshort q);
void ( * qglTexCoord4sv )(const GLshort *v);
void ( * qglTexCoordPointer )(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void ( * qglTexEnvf )(GLenum target, GLenum pname, GLfloat param);
void ( * qglTexEnvfv )(GLenum target, GLenum pname, const GLfloat *params);
void ( * qglTexEnvi )(GLenum target, GLenum pname, GLint param);
void ( * qglTexEnviv )(GLenum target, GLenum pname, const GLint *params);
void ( * qglTexGend )(GLenum coord, GLenum pname, GLdouble param);
void ( * qglTexGendv )(GLenum coord, GLenum pname, const GLdouble *params);
void ( * qglTexGenf )(GLenum coord, GLenum pname, GLfloat param);
void ( * qglTexGenfv )(GLenum coord, GLenum pname, const GLfloat *params);
void ( * qglTexGeni )(GLenum coord, GLenum pname, GLint param);
void ( * qglTexGeniv )(GLenum coord, GLenum pname, const GLint *params);
void ( * qglTexImage1D )(GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
void ( * qglTexImage2D )(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
void ( * qglTexImage2DEXT )(GLenum target, GLint level, GLint numlevels, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
void ( * qglTexParameterf )(GLenum target, GLenum pname, GLfloat param);
void ( * qglTexParameterfv )(GLenum target, GLenum pname, const GLfloat *params);
void ( * qglTexParameteri )(GLenum target, GLenum pname, GLint param);
void ( * qglTexParameteriv )(GLenum target, GLenum pname, const GLint *params);
void ( * qglTexSubImage1D )(GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type, const GLvoid *pixels);
void ( * qglTexSubImage2D )(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels);
void ( * qglTranslated )(GLdouble x, GLdouble y, GLdouble z);
void ( * qglTranslatef )(GLfloat x, GLfloat y, GLfloat z);
void ( * qglVertex2d )(GLdouble x, GLdouble y);
void ( * qglVertex2dv )(const GLdouble *v);
void ( * qglVertex2f )(GLfloat x, GLfloat y);
void ( * qglVertex2fv )(const GLfloat *v);
void ( * qglVertex2i )(GLint x, GLint y);
void ( * qglVertex2iv )(const GLint *v);
void ( * qglVertex2s )(GLshort x, GLshort y);
void ( * qglVertex2sv )(const GLshort *v);
void ( * qglVertex3d )(GLdouble x, GLdouble y, GLdouble z);
void ( * qglVertex3dv )(const GLdouble *v);
void ( * qglVertex3f )(GLfloat x, GLfloat y, GLfloat z);
void ( * qglVertex3fv )(const GLfloat *v);
void ( * qglVertex3i )(GLint x, GLint y, GLint z);
void ( * qglVertex3iv )(const GLint *v);
void ( * qglVertex3s )(GLshort x, GLshort y, GLshort z);
void ( * qglVertex3sv )(const GLshort *v);
void ( * qglVertex4d )(GLdouble x, GLdouble y, GLdouble z, GLdouble w);
void ( * qglVertex4dv )(const GLdouble *v);
void ( * qglVertex4f )(GLfloat x, GLfloat y, GLfloat z, GLfloat w);
void ( * qglVertex4fv )(const GLfloat *v);
void ( * qglVertex4i )(GLint x, GLint y, GLint z, GLint w);
void ( * qglVertex4iv )(const GLint *v);
void ( * qglVertex4s )(GLshort x, GLshort y, GLshort z, GLshort w);
void ( * qglVertex4sv )(const GLshort *v);
void ( * qglVertexPointer )(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void ( * qglViewport )(GLint x, GLint y, GLsizei width, GLsizei height);

void ( * qglMultiTexCoord2fARB )( GLenum texture, GLfloat s, GLfloat t );
void ( * qglActiveTextureARB )( GLenum texture );
void ( * qglClientActiveTextureARB )( GLenum texture );
void ( * qglLockArraysEXT )( GLint first, GLint count );
void ( * qglUnlockArraysEXT )( void );

static void _d3d_check(HRESULT err, const char* func)
{
	if (err != D3D_OK)
	{
		MEMORYSTATUS status;
		GlobalMemoryStatus(&status);
		Sys_Print(va("%s returned %d!  Memfree=%d\n", func, err, status.dwAvailPhys));
	}
}

#ifdef _WINDOWS
static bool surfaceToBMP(LPDIRECT3DDEVICE8 pd3dDevice, LPDIRECT3DSURFACE8 lpSurface, const char *fname)
{
	DWORD outpixel;
	BITMAPFILEHEADER fh;
	BITMAPINFOHEADER bi;
	int outbyte, BufferIndex, width, height, pitch;
	char *WriteBuffer;
	FILE *file;
	HRESULT Error;
	IDirect3DSurface8 *pTempSurf = NULL;

	// Get the surface description first
	D3DSURFACE_DESC ddsd;
	D3DLOCKED_RECT lrSurf;
	
	Error = lpSurface->GetDesc(&ddsd);
	// This writes out 32 bit values, so whatever surface format we were passed in,
	// copy it into a 32 bit surface
	Error = pd3dDevice->CreateImageSurface(ddsd.Width, ddsd.Height, D3DFMT_A8R8G8B8, &pTempSurf);

	Error = D3DXLoadSurfaceFromSurface(pTempSurf, NULL, NULL, lpSurface, NULL, NULL, D3DX_DEFAULT, 0);

	file = fopen(fname, "wb");
	if(!file)
		return FALSE;

	Error = pTempSurf->LockRect(&lrSurf, NULL, 0);

	BufferIndex = 0;
	width = ddsd.Width;
	height = ddsd.Height;
	pitch = lrSurf.Pitch;
	WriteBuffer = new char[width * height * 3];

	// Setup the file headers
	((char*)&(fh.bfType))[0] = 'B';
	((char*)&(fh.bfType))[1] = 'M';
	fh.bfSize = (long)(sizeof(BITMAPINFOHEADER) + sizeof(BITMAPFILEHEADER) + width * height * 3);
	fh.bfReserved1 = 0;
	fh.bfReserved2 = 0;
	fh.bfOffBits = sizeof(BITMAPINFOHEADER) + sizeof(BITMAPFILEHEADER);
	bi.biSize = sizeof(BITMAPINFOHEADER);
	bi.biWidth = width;
	bi.biHeight = height;
	bi.biPlanes = 1;
	bi.biBitCount = 24;
	bi.biCompression = BI_RGB;
	bi.biSizeImage = 0;
	bi.biXPelsPerMeter = 10000;
	bi.biYPelsPerMeter = 10000;
	bi.biClrUsed = 0;
	bi.biClrImportant = 0;

	fwrite(&fh, sizeof(BITMAPFILEHEADER), 1, file);
	fwrite(&bi, sizeof(BITMAPINFOHEADER), 1, file);

	char *Bitmap_in = (char*)lrSurf.pBits;

	for(int y = height - 1; y >= 0; y--) 
	{
		for(int x = 0; x < width; x++)
		{
			outpixel = *((DWORD *)(Bitmap_in + x * 4 + y * pitch)); //Load a word

			//Load up the Blue component and output it
			outbyte = (((outpixel)&0x000000ff));//blue
			WriteBuffer [BufferIndex++] = outbyte;

			//Load up the green component and output it 
			outbyte = (((outpixel>>8)&0x000000ff)); 
			WriteBuffer [BufferIndex++] = outbyte;

			//Load up the red component and output it 
			outbyte = (((outpixel>>16)&0x000000ff));
			WriteBuffer [BufferIndex++] = outbyte;
		}
	}

	//At this point the buffer should be full, so just write it out
	fwrite(WriteBuffer, BufferIndex, 1, file);

	//Now unlock the surface and we're done
	pTempSurf->UnlockRect();
	pTempSurf->Release();

	fclose(file);

	delete [] WriteBuffer;
	return true;
}
#endif

/*
=================
_fixupScreenCoords

Clamp coords to screen dimensions and fix Y direction.
=================
*/
static void _fixupScreenCoords(GLint& x, GLint& y, GLsizei& width, GLsizei& height)
{
	if (x < 0) x = 0;
	else if (x > glConfig.vidWidth) x = glConfig.vidWidth;
	if (y < 0)
	{
		
#ifdef _XBOX
		height += y;
#endif
		y = 0;
	}
	else if (y > glConfig.vidHeight) y = glConfig.vidHeight;
	
	if (width < 0) width = 0;
#ifdef _XBOX
	else if (x + width > glConfig.vidWidth) width = glConfig.vidWidth - x;
#endif
//	else if (x + width > glConfig.vidWidth) width = glConfig.vidWidth - x;
	if (height < 0) height = 0;
	else if (y + height > glConfig.vidHeight) height = glConfig.vidHeight - y;

	// GL and DX disagree on the direction of Y
	y = glConfig.vidHeight - (y + height);
}


/*
=================
_convertCompare

Convert GL compare function to DX function.
=================
*/
static D3DCMPFUNC _convertCompare(GLenum func)
{
	switch (func)
	{
	case GL_NEVER: return D3DCMP_NEVER;
	case GL_LESS: return D3DCMP_LESS;
	case GL_EQUAL: return D3DCMP_EQUAL;
	case GL_LEQUAL: return D3DCMP_LESSEQUAL;
	case GL_GREATER: return D3DCMP_GREATER;
	case GL_NOTEQUAL: return D3DCMP_NOTEQUAL;
	case GL_GEQUAL: return D3DCMP_GREATEREQUAL;
	default: case GL_ALWAYS: return D3DCMP_ALWAYS;
	}
}


/*
=================
_convertBlendFactor

Convert GL blend mode to DX blend mode.
=================
*/
static D3DBLEND _convertBlendFactor(GLenum factor)
{
	switch (factor)
	{
	case GL_ZERO: return D3DBLEND_ZERO;
	default: case GL_ONE: return D3DBLEND_ONE;
	case GL_SRC_COLOR: return D3DBLEND_SRCCOLOR;
	case GL_ONE_MINUS_SRC_COLOR: return D3DBLEND_INVSRCCOLOR;
	case GL_SRC_ALPHA: return D3DBLEND_SRCALPHA;
	case GL_ONE_MINUS_SRC_ALPHA: return D3DBLEND_INVSRCALPHA;
	case GL_DST_COLOR: return D3DBLEND_DESTCOLOR;
	case GL_ONE_MINUS_DST_COLOR: return D3DBLEND_INVDESTCOLOR;
	case GL_DST_ALPHA: return D3DBLEND_DESTALPHA;
	case GL_ONE_MINUS_DST_ALPHA: return D3DBLEND_INVDESTALPHA;
	case GL_SRC_ALPHA_SATURATE: return D3DBLEND_SRCALPHASAT;
	}
}


/*
=================
_convertPrimMode

Convert GL primitive mode to DX primitive mode.
=================
*/
static D3DPRIMITIVETYPE _convertPrimMode(GLenum mode)
{
	switch (mode)
	{
	case GL_POINTS: return D3DPT_POINTLIST;
	case GL_LINES: return D3DPT_LINELIST;
	case GL_LINE_STRIP: return D3DPT_LINESTRIP;
	case GL_TRIANGLES: return D3DPT_TRIANGLELIST;
	case GL_TRIANGLE_STRIP: return D3DPT_TRIANGLESTRIP;
	case GL_TRIANGLE_FAN: return D3DPT_TRIANGLEFAN;
#ifdef _XBOX
	case GL_QUADS: return D3DPT_QUADLIST;
	case GL_QUAD_STRIP: return D3DPT_QUADSTRIP;
#else
	case GL_QUADS: return D3DPT_TRIANGLELIST;
	case GL_QUAD_STRIP: return D3DPT_TRIANGLESTRIP;
#endif
	case GL_POLYGON: return D3DPT_TRIANGLEFAN;
	default: assert(0); return D3DPT_TRIANGLEFAN;
	}
}


/*
=================
_updateDrawStride

Update the stride of the draw array based on
the number of vertex attributes.  The stride
is in DWORDs.
=================
*/
static void _updateDrawStride(GLint normal, GLint tex0, GLint tex1)
{
	glw_state->drawStride = 4;
	if (normal) glw_state->drawStride += 3;
	if (tex0 || tex1) glw_state->drawStride += 2;
	if (tex1) glw_state->drawStride += 2;
}


/*
=================
_updateShader

Set the vertex shader based on the number
of texture coordinates.
=================
*/
static void _updateShader(bool normal, bool tex0, bool tex1)//, bool tex2, bool tex3)
{
	DWORD mask = D3DFVF_XYZ;
	if (normal) mask |= D3DFVF_NORMAL;
	mask |= D3DFVF_DIFFUSE;
	if (tex0 && !tex1) mask |= D3DFVF_TEX1;
	else if (tex1) mask |= D3DFVF_TEX2;

//	if (mask != glw_state->shaderMask)
//	{
		glw_state->device->SetVertexShader(mask);
		glw_state->shaderMask = mask;
//	}
}


/*
=================
_getCurrentTexture

Get the texture information for the currently
bound texture at a stage.
=================
*/
static glwstate_t::TextureInfo* _getCurrentTexture(int stage)
{
	glwstate_t::texturexlat_t::iterator i = glw_state->textureXlat.find(
		glw_state->currentTexture[stage]);

	if (i == glw_state->textureXlat.end()) return NULL;

	return &i->second;
}


/*
=================
_updateTextures

Setup texture stages with color operations, filters
and wrapping modes as needed.
=================
*/
static void _updateTextures(void)
{
	for (int t = 0; t < GLW_MAX_TEXTURE_STAGES; ++t)
	{
		if (glw_state->textureStageDirty[t])
		{
			glw_state->textureStageDirty[t] = false;

			if (glw_state->textureStageEnable[t] && glw_state->currentTexture[t])
			{
				glwstate_t::TextureInfo* info = _getCurrentTexture(t);
				if (!info) continue;

				glw_state->device->SetTexture(t, info->mipmap);
				glw_state->device->SetTextureStageState(t, D3DTSS_COLOROP, glw_state->textureEnv[t]);

				glw_state->device->SetTextureStageState(t, D3DTSS_COLORARG1, 
					D3DTA_TEXTURE);
				glw_state->device->SetTextureStageState(t, D3DTSS_COLORARG2, 
					D3DTA_CURRENT);
				glw_state->device->SetTextureStageState(t, D3DTSS_ALPHAOP, 
					glw_state->textureEnv[t]);
				glw_state->device->SetTextureStageState(t, D3DTSS_ALPHAARG1, 
					D3DTA_TEXTURE);
				glw_state->device->SetTextureStageState(t, D3DTSS_ALPHAARG2, 
					D3DTA_CURRENT);

				glw_state->device->SetTextureStageState(t, D3DTSS_MAXANISOTROPY, 
					info->anisotropy);
				glw_state->device->SetTextureStageState(t, D3DTSS_MINFILTER, 
					info->minFilter);
				glw_state->device->SetTextureStageState(t, D3DTSS_MIPFILTER, 
					info->mipFilter);
				glw_state->device->SetTextureStageState(t, D3DTSS_MAGFILTER, 
					info->magFilter);

				glw_state->device->SetTextureStageState(t, D3DTSS_ADDRESSU, 
					info->wrapU);
				glw_state->device->SetTextureStageState(t, D3DTSS_ADDRESSV, 
					info->wrapV);

				glw_state->device->SetTextureStageState(t, D3DTSS_TEXCOORDINDEX, t);

				glw_state->device->SetTextureStageState( t, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2 );

				/*if(tess.shader)
				{
					if(tess.currentPass < tess.shader->numUnfoggedPasses)
					{ 
						if(tess.shader->stages[tess.currentPass].isEnvironment)
						{
							glw_state->device->SetTextureStageState(t, D3DTSS_TEXCOORDINDEX, t | D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR);
						}
					}
				}*/				
			}
			else
			{
				glw_state->device->SetTexture(t, NULL);
				glw_state->device->SetTextureStageState(t, D3DTSS_COLOROP, D3DTOP_DISABLE);
				glw_state->device->SetTextureStageState(t, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
			}
		}
		//else
		//{
		//	// Hard-wired check for turning on hardware environment mapping
		//	if( glw_state->textureStageEnable[t] &&
		//		glw_state->currentTexture[t] &&
		//		tess.shader &&
		//		tess.currentPass < tess.shader->numUnfoggedPasses &&
		//		tess.shader->stages[tess.currentPass].isEnvironment)
		//	{
		//			glw_state->device->SetTextureStageState(t, D3DTSS_TEXCOORDINDEX, t | D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR);
		//	}
		//}
	}
}


/*
=================
_updateMatrices

Set the current projection and view transforms to
the matrices at the top of the relevant stacks.
=================
*/
static void _updateMatrices(void)
{
	if(glw_state->matricesDirty[glwstate_t::MatrixMode_Projection])
	{
		glw_state->device->SetTransform(D3DTS_PROJECTION, 
			glw_state->matrixStack[glwstate_t::MatrixMode_Projection]->GetTop());

		glw_state->matricesDirty[glwstate_t::MatrixMode_Projection] = false;
	}

	if(glw_state->matricesDirty[glwstate_t::MatrixMode_Model])
	{
		glw_state->device->SetTransform(D3DTS_VIEW, 
			glw_state->matrixStack[glwstate_t::MatrixMode_Model]->GetTop());

		glw_state->matricesDirty[glwstate_t::MatrixMode_Model] = false;
	}

	if(glw_state->matricesDirty[glwstate_t::MatrixMode_Texture0])
	{
		glw_state->device->SetTransform(D3DTS_TEXTURE0,
			glw_state->matrixStack[glwstate_t::MatrixMode_Texture0]->GetTop());

		glw_state->matricesDirty[glwstate_t::MatrixMode_Texture0] = false;
	}

	if(glw_state->matricesDirty[glwstate_t::MatrixMode_Texture1])
	{
		glw_state->device->SetTransform(D3DTS_TEXTURE1,
			glw_state->matrixStack[glwstate_t::MatrixMode_Texture1]->GetTop());

		glw_state->matricesDirty[glwstate_t::MatrixMode_Texture1] = false;
	}
}


/*
=================
_getMaxVerts

Calculate the maximum number of verts to draw
given a total number to draw, stride and max
packet size.
=================
*/
static int _getMaxVerts(void)
{
	int max = glw_state->totalVertices;
	if (max > GLW_MAX_DRAW_PACKET_SIZE / glw_state->drawStride)
	{
		max = GLW_MAX_DRAW_PACKET_SIZE / glw_state->drawStride;
	}
	return max;
}

static int _getMaxIndices(void)
{
	int max = glw_state->totalIndices;
	if(max > 1022)
		max = 1022;

	return max;
}

#ifdef _XBOX
/*
=================
_restartDrawPacket

Encode a new draw packet header into the draw array.
=================
*/
inline static DWORD* _restartDrawPacket(DWORD* packet, int verts)
{
	packet[0] = D3DPUSH_ENCODE(D3DPUSH_SET_BEGIN_END, 1);
	packet[1] = glw_state->primitiveMode;
	packet[2] = D3DPUSH_ENCODE(
		D3DPUSH_NOINCREMENT_FLAG|D3DPUSH_INLINE_ARRAY, 
		glw_state->drawStride * verts);
	return packet + 3;
}

/*
=================
_terminateDrawPacket

Finish up the last draw packet.
=================
*/
inline static DWORD* _terminateDrawPacket(DWORD* packet)
{
	packet[0] = D3DPUSH_ENCODE(D3DPUSH_SET_BEGIN_END, 1);
	packet[1] = 0;
	return packet + 2;
}

#define CMD_DRAW_INDEX_BATCH 0x1800
inline static DWORD* _restartIndexPacket(DWORD* packet, int numIndices)
{
	packet[0] = D3DPUSH_ENCODE(D3DPUSH_SET_BEGIN_END, 1);
	packet[1] = glw_state->primitiveMode;
	packet[2] = D3DPUSH_ENCODE(	D3DPUSH_NOINCREMENT_FLAG | CMD_DRAW_INDEX_BATCH, numIndices / 2 );
	return packet + 3;
}

inline static DWORD* _terminateIndexPacket(DWORD* packet)
{
	packet[0] = D3DPUSH_ENCODE(D3DPUSH_SET_BEGIN_END, 1);
	packet[1] = 0;
	return packet + 2;
}


/*
=================
_handleDrawOverflow

Prevent a draw packet from getting too
big for the hardware by restarting it as needed.
=================
*/
static void _handleDrawOverflow(void)
{
	if (s_immediateUsesUP)
	{
		if (glw_state->numVertices >= glw_state->maxVertices)
		{
			Com_Error(ERR_DROP, "Native D3D8 immediate vertex limit exceeded (%d)",
				STEFX_MAX_IMMEDIATE_VERTICES);
		}
		return;
	}

	if (glw_state->numVertices >= glw_state->maxVertices)
	{
		glw_state->drawArray += glw_state->numVertices *
			glw_state->drawStride;
		
		glw_state->totalVertices -= glw_state->numVertices;
		glw_state->maxVertices = _getMaxVerts();
		glw_state->numVertices = 0;

		glw_state->drawArray = _restartDrawPacket(
			glw_state->drawArray, glw_state->maxVertices);
	}
}
#else _XBOX
inline static DWORD* _restartDrawPacket(DWORD* packet, int verts)
{
	return packet;
}

inline static DWORD* _terminateDrawPacket(DWORD* packet)
{
	return packet;
}

static void _handleDrawOverflow(void)
{
}
#endif _XBOX


/*
=================
_vertexElement

Copy position information from the source vertex
array into a draw array.
=================
*/
#define _vertexElement(push, i)									\
{																\
	DWORD* vert = (DWORD*)((BYTE*)glw_state->vertexPointer +	\
		(i) * glw_state->vertexStride);							\
	(push)[0] = vert[0];										\
	(push)[1] = vert[1];										\
	(push)[2] = vert[2];										\
}

/*
=================
_colorElement

Copy color information from the source color
array into a draw array.
=================
*/
#define _colorElement(push, i)									\
{																\
	DWORD col = *(DWORD*)((BYTE*)glw_state->colorPointer +		\
		(i) * glw_state->colorStride);							\
	(push)[0] =													\
		((col & 0xFF000000) >> 0) |								\
		((col & 0x00FF0000) >> 16) |							\
		((col & 0x0000FF00) << 0) |								\
		((col & 0x000000FF) << 16);								\
}

/*
=================
_texCoordElement

Copy tex coord information from the source tex coord
array into a draw array.
=================
*/
#define _texCoordElement(push, i, t)							\
{																\
	DWORD* tc = (DWORD*)((BYTE*)glw_state->texCoordPointer[t] +	\
		(i) * glw_state->texCoordStride[t]);					\
	(push)[0] = tc[0];											\
	(push)[1] = tc[1];											\
}

/*
=================
_normalElement

  Copy normal information from the source normal
  array into a draw array
=================
*/
#define _normalElement(push, i)									\
{																\
	DWORD* norm = (DWORD*)((BYTE*)glw_state->normalPointer +	\
		(i) * glw_state->normalStride);							\
	(push)[0] = norm[0];										\
	(push)[1] = norm[1];										\
	(push)[2] = norm[2];										\
}


#define _tangentElement(push, i)								\
{																\
	DWORD* tang = (DWORD*)((BYTE*)&tess.tangent[i]);				\
	(push)[0] = tang[0];										\
	(push)[1] = tang[1];										\
	(push)[2] = tang[2];										\
}



/*
=========================================================
FAST INDEXED GEOMETRY DRAW LOOPS

Used by core draw routines to quickly copy
geometry from various source arrays to main
draw array.
=========================================================
*/
static void _drawElementsV(GLsizei count, const GLushort* indices)
{
	DWORD* push = glw_state->drawArray;
	for (int i = 0; i < count; ++i)
	{
		_vertexElement(&push[0], indices[i]);
		push[3] = glw_state->currentColor;
		push += 4;
	}
}

static void _drawElementsVN(GLsizei count, const GLushort* indices)
{
	DWORD* push = glw_state->drawArray;
	for (int i = 0; i < count; ++i)
	{
		_vertexElement(&push[0], indices[i]);
		_normalElement(&push[3], indices[i]);
		push[6] = glw_state->currentColor;
		push += 7;
	}
}

static void _drawElementsVC(GLsizei count, const GLushort* indices)
{
	DWORD* push = glw_state->drawArray;
	for (int i = 0; i < count; ++i)
	{
		_vertexElement(&push[0], indices[i]);
		_colorElement(&push[3], indices[i]);
		push += 4;
	}
}

static void _drawElementsVCN(GLsizei count, const GLushort* indices)
{
	DWORD* push = glw_state->drawArray;
	for (int i = 0; i < count; ++i)
	{
		_vertexElement(&push[0], indices[i]);
		_normalElement(&push[3], indices[i]);
		_colorElement(&push[6], indices[i]);
		push += 7;
	}
}

static void _drawElementsVCT(GLsizei count, const GLushort* indices)
{
	DWORD* push = glw_state->drawArray;
	for (int i = 0; i < count; ++i)
	{
		_vertexElement(&push[0], indices[i]);
		_colorElement(&push[3], indices[i]);
		_texCoordElement(&push[4], indices[i], 0);
		push += 6;
	}
}

static void _drawElementsVCNT(GLsizei count, const GLushort* indices)
{
	DWORD* push = glw_state->drawArray;
	for (int i = 0; i < count; ++i)
	{
		_vertexElement(&push[0], indices[i]);
		_normalElement(&push[3], indices[i]);
		_colorElement(&push[6], indices[i]);
		_texCoordElement(&push[7], indices[i], 0);
		push += 9;
	}
}

static void _drawElementsVCTT(GLsizei count, const GLushort* indices)
{
	DWORD* push = glw_state->drawArray;
	for (int i = 0; i < count; ++i)
	{
		_vertexElement(&push[0], indices[i]);
		_colorElement(&push[3], indices[i]);
		_texCoordElement(&push[4], indices[i], 0);
		_texCoordElement(&push[6], indices[i], 1);
		push += 8;
	}
}

static void _drawElementsVCNTT(GLsizei count, const GLushort* indices)
{
	DWORD* push = glw_state->drawArray;
	for (int i = 0; i < count; ++i)
	{
		_vertexElement(&push[0], indices[i]);
		_normalElement(&push[3], indices[i]);
		_colorElement(&push[6], indices[i]);
		_texCoordElement(&push[7], indices[i], 0);
		_texCoordElement(&push[9], indices[i], 1);
		push += 11;
	}
}

static void _drawElementsVT(GLsizei count, const GLushort* indices)
{
	DWORD* push = glw_state->drawArray;
	for (int i = 0; i < count; ++i)
	{
		_vertexElement(&push[0], indices[i]);
		push[3] = glw_state->currentColor;
		_texCoordElement(&push[4], indices[i], 0);
		push += 6;
	}
}

static void _drawElementsVNT(GLsizei count, const GLushort* indices)
{
	DWORD* push = glw_state->drawArray;
	for (int i = 0; i < count; ++i)
	{
		_vertexElement(&push[0], indices[i]);
		_normalElement(&push[3], indices[i]);
		push[6] = glw_state->currentColor;
		_texCoordElement(&push[7], indices[i], 0);
		push += 9;
	}
}

static void _drawElementsVTT(GLsizei count, const GLushort* indices)
{
	DWORD* push = glw_state->drawArray;
	for (int i = 0; i < count; ++i)
	{
		_vertexElement(&push[0], indices[i]);
		push[3] = glw_state->currentColor;
		_texCoordElement(&push[4], indices[i], 0);
		_texCoordElement(&push[6], indices[i], 1);
		push += 8;
	}
}

static void _drawElementsVNTT(GLsizei count, const GLushort* indices)
{
	DWORD* push = glw_state->drawArray;
	for (int i = 0; i < count; ++i)
	{
		_vertexElement(&push[0], indices[i]);
		_normalElement(&push[3], indices[i]);
		push[6] = glw_state->currentColor;
		_texCoordElement(&push[7], indices[i], 0);
		_texCoordElement(&push[9], indices[i], 1);
		push += 11;
	}
}


static void _drawElementsLightShader(GLsizei count, const GLushort* indices)
{
	DWORD* push = glw_state->drawArray;
	for(int i = 0; i < count; ++i)
	{
		_vertexElement(&push[0], indices[i]);
		_normalElement(&push[3], indices[i]);
		_texCoordElement(&push[6], indices[i], 0);
		_tangentElement(&push[8], indices[i]);
		push += 11;
	}
}

static void _drawElementsBumpShader(GLsizei count, const GLushort* indices)
{
	DWORD* push = glw_state->drawArray;
	for(int i = 0; i < count; ++i)
	{
		_vertexElement(&push[0], indices[i]);
		_normalElement(&push[3], indices[i]);
		_texCoordElement(&push[6], indices[i], 0);
		_texCoordElement(&push[8], indices[i], 1);
		_tangentElement(&push[10], indices[i]);
		push += 13;
	}
}

static void _drawElementsEnvShader(GLsizei count, const GLushort* indices)
{
	DWORD* push = glw_state->drawArray;
	for(int i = 0; i < count; ++i)
	{
		_vertexElement(&push[0], indices[i]);
		_normalElement(&push[3], indices[i]);
		push += 6;
	}
}

typedef void(*drawelemfunc_t)(GLsizei, const GLushort*);
static drawelemfunc_t _drawElementFuncTable[12] =
{
	_drawElementsV,
	_drawElementsVN,
	_drawElementsVT,
	_drawElementsVNT,
	_drawElementsVTT,
	_drawElementsVNTT,
	_drawElementsVC,
	_drawElementsVCN,
	_drawElementsVCT,
	_drawElementsVCNT,
	_drawElementsVCTT,
	_drawElementsVCNTT,
};



/*
=========================================================
FAST GEOMETRY DRAW LOOPS

Used by core draw routines to quickly copy
geometry from various source arrays to main
draw array.
=========================================================
*/
static void _drawArraysV(GLsizei first, GLsizei last)
{
	DWORD* push = glw_state->drawArray;
	for (int i = first; i < last; ++i)
	{
		_vertexElement(&push[0], i);
		push[3] = glw_state->currentColor;
		push += 4;
	}
}

static void _drawArraysVN(GLsizei first, GLsizei last)
{
	DWORD* push = glw_state->drawArray;
	for (int i = first; i < last; ++i)
	{
		_vertexElement(&push[0], i);
		_normalElement(&push[3], i);
		push[6] = glw_state->currentColor;
		push += 7;
	}
}

static void _drawArraysVC(GLsizei first, GLsizei last)
{
	DWORD* push = glw_state->drawArray;
	for (int i = first; i < last; ++i)
	{
		_vertexElement(&push[0], i);
		_colorElement(&push[3], i);
		push += 4;
	}
}

static void _drawArraysVCN(GLsizei first, GLsizei last)
{
	DWORD* push = glw_state->drawArray;
	for (int i = first; i < last; ++i)
	{
		_vertexElement(&push[0], i);
		_normalElement(&push[3], i);
		_colorElement(&push[6], i);
		push += 7;
	}
}

static void _drawArraysVCT(GLsizei first, GLsizei last)
{
	DWORD* push = glw_state->drawArray;
	for (int i = first; i < last; ++i)
	{
		_vertexElement(&push[0], i);
		_colorElement(&push[3], i);
		_texCoordElement(&push[4], i, 0);
		push += 6;
	}
}

static void _drawArraysVCNT(GLsizei first, GLsizei last)
{
	DWORD* push = glw_state->drawArray;
	for (int i = first; i < last; ++i)
	{
		_vertexElement(&push[0], i);
		_normalElement(&push[3], i);
		_colorElement(&push[6], i);
		_texCoordElement(&push[7], i, 0);
		push += 9;
	}
}

static void _drawArraysVCTT(GLsizei first, GLsizei last)
{
	DWORD* push = glw_state->drawArray;
	for (int i = first; i < last; ++i)
	{
		_vertexElement(&push[0], i);
		_colorElement(&push[3], i);
		_texCoordElement(&push[4], i, 0);
		_texCoordElement(&push[6], i, 1);
		push += 8;
	}
}

static void _drawArraysVCNTT(GLsizei first, GLsizei last)
{
	DWORD* push = glw_state->drawArray;
	for (int i = first; i < last; ++i)
	{
		_vertexElement(&push[0], i);
		_normalElement(&push[3], i);
		_colorElement(&push[6], i);
		_texCoordElement(&push[7], i, 0);
		_texCoordElement(&push[9], i, 1);
		push += 11;
	}
}

static void _drawArraysVT(GLsizei first, GLsizei last)
{
	DWORD* push = glw_state->drawArray;
	for (int i = first; i < last; ++i)
	{
		_vertexElement(&push[0], i);
		push[3] = glw_state->currentColor;
		_texCoordElement(&push[4], i, 0);
		push += 6;
	}
}

static void _drawArraysVNT(GLsizei first, GLsizei last)
{
	DWORD* push = glw_state->drawArray;
	for (int i = first; i < last; ++i)
	{
		_vertexElement(&push[0], i);
		_normalElement(&push[3], i);
		push[6] = glw_state->currentColor;
		_texCoordElement(&push[7], i, 0);
		push += 9;
	}
}

static void _drawArraysVTT(GLsizei first, GLsizei last)
{
	DWORD* push = glw_state->drawArray;
	for (int i = first; i < last; ++i)
	{
		_vertexElement(&push[0], i);
		push[3] = glw_state->currentColor;
		_texCoordElement(&push[4], i, 0);
		_texCoordElement(&push[6], i, 1);
		push += 8;
	}
}

static void _drawArraysVNTT(GLsizei first, GLsizei last)
{
	DWORD* push = glw_state->drawArray;
	for (int i = first; i < last; ++i)
	{
		_vertexElement(&push[0], i);
		_normalElement(&push[3], i);
		push[6] = glw_state->currentColor;
		_texCoordElement(&push[7], i, 0);
		_texCoordElement(&push[9], i, 1);
		push += 11;
	}
}

typedef void(*drawarrayfunc_t)(GLsizei, GLsizei);
static drawarrayfunc_t _drawArrayFuncTable[12] =
{
	_drawArraysV,
	_drawArraysVN,
	_drawArraysVT,
	_drawArraysVNT,
	_drawArraysVTT,
	_drawArraysVNTT,
	_drawArraysVC,
	_drawArraysVCN,
	_drawArraysVCT,
	_drawArraysVCNT,
	_drawArraysVCTT,
	_drawArraysVCNTT,
};


/*
=================
_getDrawFunc

Figure which drawing function we need based on
what vertex components we have.  Use the returned
integer to index the draw function tables.
=================
*/
static int _getDrawFunc(void)
{
	int func = 0;
	if (glw_state->colorArrayState) func += 6;
	if (glw_state->texCoordArrayState[0]) func += 2;
	if (glw_state->texCoordArrayState[1]) func += 2;
	if (glw_state->normalArrayState) ++func;
	return func;
}


static void dllAccum(GLenum op, GLfloat value)
{
	assert(false);
}

static void dllAlphaFunc(GLenum func, GLclampf ref)
{
	D3DCMPFUNC f = _convertCompare(func);
	glw_state->device->SetRenderState(D3DRS_ALPHAFUNC, f);
	glw_state->device->SetRenderState(D3DRS_ALPHAREF, (DWORD)(ref * 255.));
}

GLboolean dllAreTexturesResident(GLsizei n, const GLuint *textures, GLboolean *residences)
{
	assert(false);
	return 1;
}

static void dllArrayElement(GLint i)
{
	assert(glw_state->inDrawBlock);
	
	_handleDrawOverflow();

	DWORD* push = &glw_state->drawArray[glw_state->numVertices * 
		glw_state->drawStride];

	_vertexElement(push, i);
	push += 3;
	
	if (s_immediateHasNormal)
	{
		if (glw_state->normalArrayState)
		{
			_normalElement(push, i);
		}
		else
		{
			memcpy(push, s_immediateNormal, sizeof(s_immediateNormal));
		}
		push += 3;
	}

	if (glw_state->colorArrayState)
	{
		_colorElement(push, i);
		++push;
	}
	else
	{
		*push++ = glw_state->currentColor;
	}
	
	for (int t = 0; t < GLW_MAX_TEXTURE_STAGES; ++t)
	{
		if (s_immediateHasTexCoord[t])
		{
			if (glw_state->texCoordArrayState[t])
			{
				_texCoordElement(push, i, t);
			}
			else
			{
				memcpy(push, s_immediateTexCoord[t],
					sizeof(s_immediateTexCoord[t]));
			}
			push += 2;
		}
	}

	++glw_state->numVertices;
}

// EXTENSION: Begin a drawing block with at verts vertices
static void dllBeginEXT(GLenum mode, GLint verts, GLint colors, GLint normals, GLint tex0, GLint tex1)//, GLint tex2, GLint tex3)
{
	assert(!glw_state->inDrawBlock);
	if (!glw_state || !glw_state->device || glw_state->inDrawBlock)
	{
		g_SPXBPhaseLast = 0x494D4630; /* 'IMF0' */
		Com_Error(ERR_DROP, "Native D3D8 immediate draw began in an invalid state");
		return;
	}
	if (verts < 0 || verts > STEFX_MAX_IMMEDIATE_VERTICES)
	{
		g_SPXBPhaseLast = 0x494D4631; /* 'IMF1' */
		Com_Error(ERR_DROP, "Native D3D8 immediate draw has invalid vertex count %d", verts);
		return;
	}

	// start the draw block
	glw_state->inDrawBlock = true;
	glw_state->primitiveMode = _convertPrimMode(mode);
	s_immediateHasNormal = normals != 0;
	s_immediateHasTexCoord[0] = tex0 != 0 || tex1 != 0;
	s_immediateHasTexCoord[1] = tex1 != 0;

	// update DX with any pending state changes
	_updateDrawStride(s_immediateHasNormal,
		s_immediateHasTexCoord[0], s_immediateHasTexCoord[1]);
	_updateShader(s_immediateHasNormal,
		s_immediateHasTexCoord[0], s_immediateHasTexCoord[1]);
	_updateTextures();
	_updateMatrices();

	// set vertex counters
	glw_state->numVertices = 0;
	glw_state->totalVertices = verts;
	glw_state->maxVertices = STEFX_MAX_IMMEDIATE_VERTICES;
	glw_state->drawArray = s_immediateVertices;
	s_immediateUsesUP = true;
}

static void dllBegin(GLenum mode)
{
	// Unsized immediate draws still exist in effects and diagnostic paths.
	// Use the complete fixed-function layout so their current state is safe.
	dllBeginEXT(mode, STEFX_MAX_IMMEDIATE_VERTICES,
		STEFX_MAX_IMMEDIATE_VERTICES, STEFX_MAX_IMMEDIATE_VERTICES,
		STEFX_MAX_IMMEDIATE_VERTICES, STEFX_MAX_IMMEDIATE_VERTICES);
}

// EXTENSION: Start a new drawing frame
GLboolean dllBeginFrame(void)
{
	g_SPXBPhaseLast = 0x42463030; /* 'BF00' */
	GLboolean result = glw_state->device->BeginScene() == D3D_OK;
	g_SPXBPhaseLast = result ? 0x42463031 : 0x42464631; /* 'BF01'/'BFF1' */
	return result;
}

// EXTENSION: Begin shadow draw mode
static void dllBeginShadow(void)
{
	//Intentionally left blank
}

static void dllBindTexture(GLenum target, GLuint texture)
{
	assert(target == GL_TEXTURE_2D);

	if (glw_state->currentTexture[glw_state->serverTU] != texture)
	{
		glw_state->currentTexture[glw_state->serverTU] = texture;
		glw_state->textureStageDirty[glw_state->serverTU] = true;
	}
}

static void dllBitmap(GLsizei width, GLsizei height, GLfloat xorig, GLfloat yorig, GLfloat xmove, GLfloat ymove, const GLubyte *bitmap)
{
	assert(false);
}

static void dllBlendFunc(GLenum sfactor, GLenum dfactor)
{
	D3DBLEND s = _convertBlendFactor(sfactor);
	D3DBLEND d = _convertBlendFactor(dfactor);
	
	glw_state->device->SetRenderState(D3DRS_SRCBLEND, s);
	glw_state->device->SetRenderState(D3DRS_DESTBLEND, d);
}

static void dllCallList(GLuint lnum)
{
	assert(0);
}

static void dllCallLists(GLsizei n, GLenum type, const GLvoid *lists)
{
	assert(0);
}

static void dllClear(GLbitfield mask)
{
	DWORD m = 0;

	if (mask & GL_COLOR_BUFFER_BIT) m |= D3DCLEAR_TARGET;
	if (mask & GL_STENCIL_BUFFER_BIT) m |= D3DCLEAR_STENCIL;

#ifdef _XBOX
	// Clearing stencil when clearing depth buffer
	// is faster on Xbox than just clearing depth alone.
	if (mask & GL_DEPTH_BUFFER_BIT) m |= D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL;
#else
	if (mask & GL_DEPTH_BUFFER_BIT) m |= D3DCLEAR_ZBUFFER;
#endif
	
	glw_state->device->Clear(0, NULL, m, glw_state->clearColor, 
		glw_state->clearDepth, glw_state->clearStencil);
}

static void dllClearAccum(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
	assert(0);
}

static void dllClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
	glw_state->clearColor = D3DCOLOR_COLORVALUE(red, green, blue, alpha);
}

static void dllClearDepth(GLclampd depth)
{
	glw_state->clearDepth = depth;
}

static void dllClearIndex(GLfloat c)
{
	assert(0);
}

static void dllClearStencil(GLint s)
{
	glw_state->clearStencil = s;
}

static void dllClipPlane(GLenum plane, const GLdouble *equation)
{
	//FIXME
}

static void setIntColor(GLbyte red, GLbyte green, GLbyte blue, GLbyte alpha)
{
	glw_state->currentColor = D3DCOLOR_RGBA(red, green, blue, alpha);
}

static void setFloatColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
	glw_state->currentColor = D3DCOLOR_COLORVALUE(red, green, blue, alpha);
}

static void dllColor3b(GLbyte red, GLbyte green, GLbyte blue)
{
	setIntColor(red, green, blue, 127);
}

static void dllColor3bv(const GLbyte *v)
{
	setIntColor(v[0], v[1], v[2], 127);
}

static void dllColor3d(GLdouble red, GLdouble green, GLdouble blue)
{
	setFloatColor(red, green, blue, 1.f);
}

static void dllColor3dv(const GLdouble *v)
{
	setFloatColor(v[0], v[1], v[2], 1.f);
}

static void dllColor3f(GLfloat red, GLfloat green, GLfloat blue)
{
	setFloatColor(red, green, blue, 1.f);
}

static void dllColor3fv(const GLfloat *v)
{
	setFloatColor(v[0], v[1], v[2], 1.f);
}

static void dllColor3i(GLint red, GLint green, GLint blue)
{
	setIntColor(red, green, blue, 127);
}

static void dllColor3iv(const GLint *v)
{
	setIntColor(v[0], v[1], v[2], 127);
}

static void dllColor3s(GLshort red, GLshort green, GLshort blue)
{
	setIntColor(red, green, blue, 127);
}

static void dllColor3sv(const GLshort *v)
{
	setIntColor(v[0], v[1], v[2], 127);
}

static void dllColor3ub(GLubyte red, GLubyte green, GLubyte blue)
{
	setIntColor(red, green, blue, 127);
}

static void dllColor3ubv(const GLubyte *v)
{
	setIntColor(v[0], v[1], v[2], 127);
}

static void dllColor3ui(GLuint red, GLuint green, GLuint blue)
{
	setIntColor(red, green, blue, 127);
}

static void dllColor3uiv(const GLuint *v)
{
	setIntColor(v[0], v[1], v[2], 127);
}

static void dllColor3us(GLushort red, GLushort green, GLushort blue)
{
	setIntColor(red, green, blue, 127);
}

static void dllColor3usv(const GLushort *v)
{
	setIntColor(v[0], v[1], v[2], 127);
}

static void dllColor4b(GLbyte red, GLbyte green, GLbyte blue, GLbyte alpha)
{
	setIntColor(red, green, blue, alpha);
}

static void dllColor4bv(const GLbyte *v)
{
	setIntColor(v[0], v[1], v[2], v[3]);
}

static void dllColor4d(GLdouble red, GLdouble green, GLdouble blue, GLdouble alpha)
{
	setFloatColor(red, green, blue, alpha);
}

static void dllColor4dv(const GLdouble *v)
{
	setFloatColor(v[0], v[1], v[2], v[3]);
}

static void dllColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
	setFloatColor(red, green, blue, alpha);
}

static void dllColor4fv(const GLfloat *v)
{
	setFloatColor(v[0], v[1], v[2], v[3]);
}

static void dllColor4i(GLint red, GLint green, GLint blue, GLint alpha)
{
	setIntColor(red, green, blue, alpha);
}

static void dllColor4iv(const GLint *v)
{
	setIntColor(v[0], v[1], v[2], v[3]);
}

static void dllColor4s(GLshort red, GLshort green, GLshort blue, GLshort alpha)
{
	setIntColor(red, green, blue, alpha);
}

static void dllColor4sv(const GLshort *v)
{
	setIntColor(v[0], v[1], v[2], v[3]);
}

static void dllColor4ub(GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha)
{
	setIntColor(red, green, blue, alpha);
}

static void dllColor4ubv(const GLubyte *v)
{
	setIntColor(v[0], v[1], v[2], v[3]);
}

static void dllColor4ui(GLuint red, GLuint green, GLuint blue, GLuint alpha)
{
	setIntColor(red, green, blue, alpha);
}

static void dllColor4uiv(const GLuint *v)
{
	setIntColor(v[0], v[1], v[2], v[3]);
}

static void dllColor4us(GLushort red, GLushort green, GLushort blue, GLushort alpha)
{
	setIntColor(red, green, blue, alpha);
}

static void dllColor4usv(const GLushort *v)
{
	setIntColor(v[0], v[1], v[2], v[3]);
}

static void dllColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha)
{
	DWORD m = 0;
	if (red) m |= D3DCOLORWRITEENABLE_RED;
	if (green) m |= D3DCOLORWRITEENABLE_GREEN;
	if (blue) m |= D3DCOLORWRITEENABLE_BLUE;
	if (alpha) m |= D3DCOLORWRITEENABLE_ALPHA;
	glw_state->device->SetRenderState(D3DRS_COLORWRITEENABLE, m);
}

static void dllColorMaterial(GLenum face, GLenum mode)
{
	assert(0);
}

static void dllColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
	assert(!glw_state->inDrawBlock);
	assert(size == 4 && type == GL_UNSIGNED_BYTE);
	
	stride = (stride == 0) ? sizeof(GLint) : stride;

	glw_state->colorPointer = pointer; 
	glw_state->colorStride = stride;
}

static void dllCopyPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum type)
{
	assert(0);
}

static void dllCopyTexImage1D(GLenum target, GLint level, GLenum internalFormat, GLint x, GLint y, GLsizei width, GLint border)
{
	assert(0);
}

/**********
copies a portion of the backbuffer to the current texture.
the current texture must be a linear format texture, if
a swizzled texture format is needed, use
dllCopyBackBufferToTexEXT
**********/
static void dllCopyTexImage2D(GLenum target, GLint level, GLenum internalFormat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border)
{
	// check to make sure everything passed in is supported
	assert((target == GL_TEXTURE_2D) && (level == 0) && (border == 0));

	// locals
	RECT						rSrc;
	POINT						ptUpperLeft;
	LPDIRECT3DSURFACE8			tSurf;
	LPDIRECT3DSURFACE8			backbuffer;
	glwstate_t::TextureInfo*	tex;
	HRESULT						res;
		
	// get the current texture
	tex = _getCurrentTexture(glw_state->serverTU);
	if (tex == NULL)
	{
		return;
	}

	// set up the source rectangle
	rSrc.left	= x;
	rSrc.right	= x + width;
	rSrc.top	= (480 - y) - height;
	rSrc.bottom	= (480 - y);

	// set up the target point
	ptUpperLeft.x	= 0;
	ptUpperLeft.y	= 0;

	// attach the current texture to a surface
	tex->mipmap->GetSurfaceLevel(0, &tSurf);

	// attach the back buffer to a surface
	res = glw_state->device->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &backbuffer);

	// copy the data
	res = glw_state->device->CopyRects(backbuffer, &rSrc, 0, tSurf, &ptUpperLeft);

	// release surfaces
	tSurf->Release();
	backbuffer->Release();
}

static void dllCopyTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLint x, GLint y, GLsizei width)
{
	assert(0);
}

static void dllCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height)
{
	assert(0);
}

static void dllCullFace(GLenum mode)
{
	switch (mode)
	{
	default: case GL_BACK: glw_state->cullMode = D3DCULL_CW; break;
	case GL_FRONT: glw_state->cullMode = D3DCULL_CCW; break;
	}

	glw_state->device->SetRenderState(D3DRS_CULLMODE, glw_state->cullMode);
}

static void dllDeleteLists(GLuint lnum, GLsizei range)
{
	assert(0);
}

static void dllDeleteTextures(GLsizei n, const GLuint *textures)
{
	glw_state->textureStageDirty[0] = true;
	glw_state->textureStageDirty[1] = true;
	glw_state->device->SetTexture(0, NULL);
	glw_state->device->SetTexture(1, NULL);
//	glw_state->device->SetTexture(2, NULL);
//	glw_state->device->SetTexture(3, NULL);

	for (int t = 0; t < n; ++t)
	{
		glwstate_t::texturexlat_t::iterator i = 
			glw_state->textureXlat.find(textures[t]);

		if (i != glw_state->textureXlat.end())
		{
#if MEMORY_PROFILE
			texMemSize -= getTexMemSize(i->second.mipmap);
#endif
			i->second.mipmap->BlockUntilNotBusy();
			delete i->second.mipmap;
			if (i->second.data)
			{
				if (!gTextures.Free(i->second.data, i->second.dataSize))
				{
					XBLF("Native D3D8 texture free rejected: tex=%u data=%p size=%u",
						(unsigned)textures[t], i->second.data,
						(unsigned)i->second.dataSize);
				}
				i->second.data = NULL;
				i->second.dataSize = 0;
			}
			glw_state->textureXlat.erase(i);
		}
	}
}

static void dllDepthFunc(GLenum func)
{
	D3DCMPFUNC f = _convertCompare(func);
	glw_state->device->SetRenderState(D3DRS_ZFUNC, f);
}

static void dllDepthMask(GLboolean flag)
{
	glw_state->device->SetRenderState(D3DRS_ZWRITEENABLE, flag);
}

static void dllDepthRange(GLclampd zNear, GLclampd zFar)
{
	glw_state->viewport.MinZ = zNear;
	glw_state->viewport.MaxZ = zFar;
	glw_state->device->SetViewport(&glw_state->viewport);
}

#ifdef _XBOX
static void fillPresentParameters(D3DPRESENT_PARAMETERS* pp, int width,
	int height, bool vsync)
{
	memset(pp, 0, sizeof(*pp));
	pp->BackBufferWidth = width;
	pp->BackBufferHeight = height;
	pp->BackBufferFormat = D3DFMT_A8R8G8B8;
	pp->BackBufferCount = 1;
	pp->MultiSampleType = D3DMULTISAMPLE_NONE;
	pp->SwapEffect = D3DSWAPEFFECT_DISCARD;
	pp->hDeviceWindow = 0;
	pp->Windowed = FALSE;
	pp->EnableAutoDepthStencil = TRUE;
	pp->AutoDepthStencilFormat = D3DFMT_LIN_D24S8;
	pp->Flags = 0;

	if (glw_state && glw_state->isWidescreen)
	{
		pp->Flags |= D3DPRESENTFLAG_WIDESCREEN;
		if (XGetVideoFlags() & XC_VIDEO_FLAGS_HDTV_480p)
			pp->Flags |= D3DPRESENTFLAG_PROGRESSIVE;
	}

	pp->FullScreen_RefreshRateInHz = D3DPRESENT_RATE_DEFAULT;
	pp->FullScreen_PresentationInterval = vsync
		? D3DPRESENT_INTERVAL_DEFAULT
		: D3DPRESENT_INTERVAL_IMMEDIATE;
}

static void setPresent(bool vsync)
{
	g_SPXBPhaseLast = 0x50523030; /* 'PR00' */
	const DWORD interval = vsync
		? D3DPRESENT_INTERVAL_DEFAULT
		: D3DPRESENT_INTERVAL_IMMEDIATE;
	const HRESULT intervalResult = glw_state->device->SetRenderState(
		D3DRS_PRESENTATIONINTERVAL, interval);
	if (FAILED(intervalResult))
	{
		g_SPXBPhaseLast = 0x50524630; /* 'PRF0' */
		Com_Error(ERR_FATAL,
			"Native D3D8 presentation interval change failed: 0x%08x",
			(unsigned)intervalResult);
		return;
	}
	g_SPXBPhaseLast = 0x50523032; /* 'PR02' */
}
#endif

static void setCap(GLenum cap, bool flag)
{
	switch (cap)
	{
	case GL_ALPHA_TEST: glw_state->device->SetRenderState(D3DRS_ALPHATESTENABLE, flag); break;
	case GL_BLEND: glw_state->device->SetRenderState(D3DRS_ALPHABLENDENABLE, flag); break;
	case GL_CULL_FACE:
		glw_state->cullEnable = flag;
		glw_state->device->SetRenderState(D3DRS_CULLMODE, 
			flag ? glw_state->cullMode : D3DCULL_NONE);
		break;
	case GL_DEPTH_TEST: glw_state->device->SetRenderState(D3DRS_ZENABLE, flag); break;
	case GL_LIGHTING: glw_state->device->SetRenderState(D3DRS_LIGHTING, flag); break;
#ifdef _XBOX
	case GL_POLYGON_OFFSET_POINT:
		glw_state->device->SetRenderState(D3DRS_POINTOFFSETENABLE, flag);
		break;
	case GL_POLYGON_OFFSET_LINE:
		glw_state->device->SetRenderState(D3DRS_WIREFRAMEOFFSETENABLE, flag);
		break;
	case GL_POLYGON_OFFSET_FILL:
		glw_state->device->SetRenderState(D3DRS_SOLIDOFFSETENABLE, flag);
		break;
	case GL_SCISSOR_TEST:
		glw_state->scissorEnable = flag;
		glw_state->device->SetScissors(flag ? 1 : 0, FALSE, &glw_state->scissorBox);
		break;
#endif
	case GL_STENCIL_TEST: glw_state->device->SetRenderState(D3DRS_STENCILENABLE, flag); break;
	case GL_TEXTURE_2D: 
		glw_state->textureStageEnable[glw_state->serverTU] = flag;
		glw_state->textureStageDirty[glw_state->serverTU] = true;
		break;
	case GL_FOG:
		glw_state->device->SetRenderState(D3DRS_FOGENABLE, flag);
		break;
#ifdef _XBOX
	case GL_VSYNC:
		setPresent(flag);
		break;
#endif
	default: break;
	}
}

static void dllDisable(GLenum cap)
{
	setCap(cap, false);
}

static void setArrayState(GLenum cap, bool state)
{
	switch (cap)
	{
	case GL_COLOR_ARRAY: glw_state->colorArrayState = state; break; 
	case GL_TEXTURE_COORD_ARRAY: glw_state->texCoordArrayState[glw_state->clientTU] = state; break;
	case GL_VERTEX_ARRAY: glw_state->vertexArrayState = state; break;
	case GL_NORMAL_ARRAY: glw_state->normalArrayState = state; break;
	}
}

static void dllDisableClientState(GLenum array)
{
	assert(!glw_state->inDrawBlock);
	setArrayState(array, false);
}

#ifdef _WINDOWS
static void _convertQuadsToTris(GLint first, GLsizei count)
{
	glw_state->vertexPointerBack = glw_state->vertexPointer;
	glw_state->normalPointerBack = glw_state->normalPointer;
	glw_state->colorPointerBack = glw_state->colorPointer;
	glw_state->texCoordPointerBack[0] = glw_state->texCoordPointer[0];
	glw_state->texCoordPointerBack[1] = glw_state->texCoordPointer[1];
	
	{
		glw_state->vertexPointer = 
			Z_Malloc(count * glw_state->vertexStride * 3 / 2, 
			TAG_TEMP_WORKSPACE, qfalse);
		for (int i = 0; i < count; i += 4)
		{
			int stride = glw_state->vertexStride / sizeof(float);
			float* dst = (float*)glw_state->vertexPointer + (i * 3 / 2) * stride;
			const float* src = (const float*)glw_state->vertexPointerBack + 
				(first + i) * stride;
			
			for (int j = 0; j < 3; ++j)
			{
				dst[0 * stride + j] = src[0 * stride + j];
				dst[1 * stride + j] = src[1 * stride + j];
				dst[2 * stride + j] = src[2 * stride + j];
				dst[3 * stride + j] = src[0 * stride + j];
				dst[4 * stride + j] = src[2 * stride + j];
				dst[5 * stride + j] = src[3 * stride + j];
			}
		}
	}
	
	if (glw_state->normalArrayState)
	{
		glw_state->normalPointer = 
			Z_Malloc(count * glw_state->normalStride * 3 / 2, 
			TAG_TEMP_WORKSPACE, qfalse);
		
		for (int i = 0; i < count; i += 4)
		{
			int stride = glw_state->normalStride / sizeof(float);
			float* dst = (float*)glw_state->normalPointer + (i * 3 / 2) * stride;
			const float* src = (const float*)glw_state->normalPointerBack + 
				(first + i) * stride;
			
			for (int j = 0; j < 3; ++j)
			{
				dst[0 * stride + j] = src[0 * stride + j];
				dst[1 * stride + j] = src[1 * stride + j];
				dst[2 * stride + j] = src[2 * stride + j];
				dst[3 * stride + j] = src[0 * stride + j];
				dst[4 * stride + j] = src[2 * stride + j];
				dst[5 * stride + j] = src[3 * stride + j];
			}
		}
	}
	
	if (glw_state->colorArrayState)
	{
		glw_state->colorPointer = 
			Z_Malloc(count * glw_state->colorStride * 3 / 2, 
			TAG_TEMP_WORKSPACE, qfalse);
		
		for (int i = 0; i < count; i += 4)
		{
			int stride = glw_state->colorStride / sizeof(DWORD);
			DWORD* dst = (DWORD*)glw_state->colorPointer + (i * 3 / 2) * stride;
			const DWORD* src = (const DWORD*)glw_state->colorPointerBack + 
				(first + i) * stride;
			
			dst[0 * stride] = src[0 * stride];
			dst[1 * stride] = src[1 * stride];
			dst[2 * stride] = src[2 * stride];
			dst[3 * stride] = src[0 * stride];
			dst[4 * stride] = src[2 * stride];
			dst[5 * stride] = src[3 * stride];
		}
	}
	
	for (int t = 0; t < GLW_MAX_TEXTURE_STAGES; ++t)
	{
		if (glw_state->texCoordArrayState[t])
		{
			glw_state->texCoordPointer[t] = 
				Z_Malloc(count * glw_state->texCoordStride[t] * 3 / 2, 
				TAG_TEMP_WORKSPACE, qfalse);
			
			for (int i = 0; i < count; i += 4)
			{
				int stride = glw_state->texCoordStride[t] / sizeof(float);
				float* dst = (float*)glw_state->texCoordPointer[t] + (i * 3 / 2) * stride;
				const float* src = (const float*)glw_state->texCoordPointerBack[t] + 
					(first + i) * stride;
				
				for (int j = 0; j < 2; ++j)
				{
					dst[0 * stride + j] = src[0 * stride + j];
					dst[1 * stride + j] = src[1 * stride + j];
					dst[2 * stride + j] = src[2 * stride + j];
					dst[3 * stride + j] = src[0 * stride + j];
					dst[4 * stride + j] = src[2 * stride + j];
					dst[5 * stride + j] = src[3 * stride + j];
				}
			}
		}
	}
}

static void _cleanupQuadsToTris(void)
{
	Z_Free(const_cast<void*>(glw_state->vertexPointer));
	glw_state->vertexPointer = glw_state->vertexPointerBack;
	
	if (glw_state->normalArrayState)
	{
		Z_Free(const_cast<void*>(glw_state->normalPointer));
		glw_state->normalPointer = glw_state->normalPointerBack;
	}
	
	if (glw_state->colorArrayState)
	{
		Z_Free(const_cast<void*>(glw_state->colorPointer));
		glw_state->colorPointer = glw_state->colorPointerBack;
	}

	for (int t = 0; t < GLW_MAX_TEXTURE_STAGES; ++t)
	{
		if (glw_state->texCoordArrayState[t])
		{
			Z_Free(const_cast<void*>(glw_state->texCoordPointer[t]));
			glw_state->texCoordPointer[t] = glw_state->texCoordPointerBack[t];
		}
	}
}
#endif

/*
=================
dllDrawArraysUP

Supported D3D8 submission for non-indexed arrays. This covers quick sprites
without exposing their vertex data as executable pushbuffer memory.
=================
*/
static void dllDrawArraysUP(GLenum mode, GLint first, GLsizei count)
{
	enum { STEFX_MAX_NATIVE_VERTEX_DWORDS = 11 };
	static DWORD s_nativeVertices[SHADER_MAX_VERTEXES * STEFX_MAX_NATIVE_VERTEX_DWORDS];
	const bool normals = glw_state->normalArrayState != 0;
	const bool tex0Array = glw_state->texCoordArrayState[0] != 0;
	const bool tex1Array = glw_state->texCoordArrayState[1] != 0;
	const bool tex0Layout = tex0Array || tex1Array;
	DWORD *out = s_nativeVertices;

	if (first < 0 || count < 0 || first > SHADER_MAX_VERTEXES ||
		count > SHADER_MAX_VERTEXES - first)
	{
		g_SPXBPhaseLast = 0x44414630; /* 'DAF0' */
		Com_Error(ERR_FATAL,
			"Native D3D8 array draw out of range: first=%d count=%d max=%d",
			first, count, SHADER_MAX_VERTEXES);
		return;
	}
	if (!glw_state->vertexArrayState || !glw_state->vertexPointer)
	{
		g_SPXBPhaseLast = 0x44414631; /* 'DAF1' */
		Com_Error(ERR_FATAL, "Native D3D8 array draw has no vertex source");
		return;
	}
	if ((normals && !glw_state->normalPointer) ||
		(glw_state->colorArrayState && !glw_state->colorPointer) ||
		(tex0Array && !glw_state->texCoordPointer[0]) ||
		(tex1Array && !glw_state->texCoordPointer[1]))
	{
		g_SPXBPhaseLast = 0x44414632; /* 'DAF2' */
		Com_Error(ERR_FATAL, "Native D3D8 array draw has an invalid enabled source");
		return;
	}

	glw_state->inDrawBlock = true;
	glw_state->primitiveMode = _convertPrimMode(mode);
	_updateDrawStride(normals, tex0Layout, tex1Array);
	if (glw_state->drawStride <= 0 ||
		glw_state->drawStride > STEFX_MAX_NATIVE_VERTEX_DWORDS)
	{
		g_SPXBPhaseLast = 0x44414633; /* 'DAF3' */
		Com_Error(ERR_FATAL, "Native D3D8 array stride invalid: %d",
			glw_state->drawStride);
		glw_state->inDrawBlock = false;
		return;
	}
	_updateShader(normals, tex0Layout, tex1Array);
	_updateTextures();
	_updateMatrices();

	for (int vertex = first; vertex < first + count; ++vertex)
	{
		_vertexElement(out, vertex);
		out += 3;

		if (normals)
		{
			_normalElement(out, vertex);
			out += 3;
		}

		if (glw_state->colorArrayState)
		{
			if (glw_state->colorPointer == tess.svars.colors)
			{
				*out = tess.svars.colors[vertex];
			}
			else
			{
				_colorElement(out, vertex);
			}
		}
		else
		{
			*out = glw_state->currentColor;
		}
		++out;

		if (tex0Layout)
		{
			if (tex0Array)
			{
				_texCoordElement(out, vertex, 0);
			}
			else
			{
				memcpy(out, s_immediateTexCoord[0],
					sizeof(s_immediateTexCoord[0]));
			}
			out += 2;
		}

		if (tex1Array)
		{
			_texCoordElement(out, vertex, 1);
			out += 2;
		}
	}

	assert(out == s_nativeVertices + count * glw_state->drawStride);

	if (!SubmitNativeVerticesRing(glw_state->primitiveMode,
			s_nativeVertices, count,
			glw_state->drawStride * sizeof(DWORD)))
	{
		Com_Error(ERR_FATAL,
			"Native D3D8 array vertex-ring submission failed");
	}
	glw_state->drawArray = NULL;
	glw_state->inDrawBlock = false;
}

// Retained as a reference for a later persistent-buffer fast path.
static void dllDrawArraysPush(GLenum mode, GLint first, GLsizei count)
{
#ifdef _WINDOWS
	if (mode == GL_QUADS)
	{
		_convertQuadsToTris(first, count);
		count = count * 3 / 2;
		first = 0;
	}
#endif

	// start the draw mode
	qglBeginEXT(mode, count, glw_state->colorArrayState ? count : 0, 
		glw_state->normalArrayState ? count : 0,
		glw_state->texCoordArrayState[0] ? count : 0,
		glw_state->texCoordArrayState[1] ? count : 0);

	// get the draw function we need
	drawarrayfunc_t func = _drawArrayFuncTable[_getDrawFunc()];

#ifndef _XBOX
	DWORD* base = glw_state->drawArray;
#endif

	int inc = glw_state->maxVertices;
	// loop taking care not to draw too much at a time
	for (int start = first; ; start += inc)//glw_state->maxVertices)
	{
		// draw glw_state->maxVertices amount of geometry
		func(start, start + glw_state->maxVertices);
		
		// are we done yet?
		glw_state->totalVertices -= glw_state->maxVertices;
		if (glw_state->totalVertices <= 0)
		{
			glw_state->numVertices = glw_state->maxVertices;
			break;
		}
		
		// ready for another cycle
		glw_state->drawArray += glw_state->maxVertices *
			glw_state->drawStride;
		glw_state->maxVertices = _getMaxVerts();

		glw_state->drawArray = _restartDrawPacket(
			glw_state->drawArray, glw_state->maxVertices); 
	}
	
#ifndef _XBOX
	glw_state->drawArray = base;
#endif

#ifdef _WINDOWS
	if (mode == GL_QUADS)
	{
		_cleanupQuadsToTris();
	}
#endif

	// finish up the draw
	qglEnd();
}

// NOTE: This is a core draw routine. It must remain on a native D3D8 path.
static void dllDrawArrays(GLenum mode, GLint first, GLsizei count)
{
	dllDrawArraysUP(mode, first, count);
}

static void dllDrawBuffer(GLenum mode)
{
	//FIXME
}


static void PushIndices(GLsizei count, const GLushort *indices)
{
	// open the index packet
	// can only send 2047 indices thru at a time
	// BUT, Microsoft recommends 511 pairs at a time (?)
	int num_packets, numpairs;
	bool singleindex = false;

	numpairs = count / 2;

	if(numpairs <= 511) 
	{
		num_packets = 1;

		if(glw_state->maxIndices % 2)
		{
			glw_state->maxIndices -= 1;
			singleindex = true;
		}
	} else 
	{
		num_packets = (count / glw_state->maxIndices) + (!!(count % glw_state->maxIndices));
	}
	
	glw_state->drawArray = _restartIndexPacket(glw_state->drawArray, glw_state->maxIndices);

	int inc = glw_state->maxIndices;
	for (int start = 0; ; start += inc)
	{
		// memcpy is faster than looping copy:
		memcpy( glw_state->drawArray, indices+start, glw_state->maxIndices * sizeof(WORD) );
		glw_state->drawArray += glw_state->maxIndices / 2;

		/*
		for(int i = start; i < start + glw_state->maxIndices; i += 2)
		{
			*glw_state->drawArray++ = (DWORD)(((WORD)indices[i + 1] << 16) + (WORD)indices[i]);
		}
		*/

		// are we done yet?
		glw_state->totalIndices -= glw_state->maxIndices;
		if (glw_state->totalIndices <= 1)
		{
			glw_state->numIndices = glw_state->maxIndices;
			break;
		}

		// ready for another cycle
		//glw_state->drawArray += glw_state->maxVertices * glw_state->drawStride;
		glw_state->maxIndices = _getMaxIndices();

		if(glw_state->maxIndices % 2)
		{
			glw_state->maxIndices -= 1;
			singleindex = true;
		}

		glw_state->drawArray = _restartIndexPacket(glw_state->drawArray, glw_state->maxIndices);
	}

#define CMD_DRAW_INDEX_LAST 0x1808 
	if(singleindex)
	{
		*glw_state->drawArray++ = D3DPUSH_ENCODE(CMD_DRAW_INDEX_LAST, 1);
		*glw_state->drawArray++ = indices[count - 1];
	}
}

/*
=================
dllDrawElementsUP

Submit the tessellator through the supported Xbox D3D8 user-pointer API.
This is the correctness baseline for the native renderer: D3D owns the
pushbuffer encoding and no stream points back into transient command memory.
=================
*/
static void dllDrawElementsUP(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
	enum { STEFX_MAX_NATIVE_VERTEX_DWORDS = 11 };
	static DWORD s_nativeVertices[SHADER_MAX_VERTEXES * STEFX_MAX_NATIVE_VERTEX_DWORDS];
	const bool normals = glw_state->normalArrayState != 0;
	const bool tex0Array = glw_state->texCoordArrayState[0] != 0;
	const bool tex1Array = glw_state->texCoordArrayState[1] != 0;
	const bool tex0Layout = tex0Array || tex1Array;
	const GLushort *sourceIndices = (const GLushort *)indices;
	DWORD *out = s_nativeVertices;
	int sourceVertexCount = 0;

	if (type != GL_UNSIGNED_SHORT || count < 0 ||
		count > SHADER_MAX_VERTEXES || !indices)
	{
		g_SPXBPhaseLast = 0x44494630; /* 'DIF0' */
		Com_Error(ERR_FATAL,
			"Native D3D8 indexed draw invalid: type=0x%x count=%d indices=%p",
			type, count, indices);
		return;
	}
	if (!glw_state->vertexArrayState || !glw_state->vertexPointer)
	{
		g_SPXBPhaseLast = 0x44494631; /* 'DIF1' */
		Com_Error(ERR_FATAL, "Native D3D8 indexed draw has no vertex source");
		return;
	}
	if ((normals && !glw_state->normalPointer) ||
		(glw_state->colorArrayState && !glw_state->colorPointer) ||
		(tex0Array && !glw_state->texCoordPointer[0]) ||
		(tex1Array && !glw_state->texCoordPointer[1]))
	{
		g_SPXBPhaseLast = 0x44494632; /* 'DIF2' */
		Com_Error(ERR_FATAL, "Native D3D8 indexed draw has an invalid enabled source");
		return;
	}
	for (int index = 0; index < count; ++index)
	{
		const int vertex = sourceIndices[index];
		if (vertex >= SHADER_MAX_VERTEXES)
		{
			g_SPXBPhaseLast = 0x44494633; /* 'DIF3' */
			Com_Error(ERR_FATAL,
				"Native D3D8 index out of range: index=%d vertex=%d max=%d",
				index, vertex, SHADER_MAX_VERTEXES);
			return;
		}
		if (sourceVertexCount <= vertex)
			sourceVertexCount = vertex + 1;
	}

	glw_state->inDrawBlock = true;
	glw_state->primitiveMode = _convertPrimMode(mode);

	_updateDrawStride(normals, tex0Layout, tex1Array);
	if (glw_state->drawStride <= 0 ||
		glw_state->drawStride > STEFX_MAX_NATIVE_VERTEX_DWORDS)
	{
		g_SPXBPhaseLast = 0x44494634; /* 'DIF4' */
		Com_Error(ERR_FATAL, "Native D3D8 indexed stride invalid: %d",
			glw_state->drawStride);
		glw_state->inDrawBlock = false;
		return;
	}
	_updateShader(normals, tex0Layout, tex1Array);
	_updateTextures();
	_updateMatrices();

	for (int index = 0; index < count; ++index)
	{
		const int vertex = sourceIndices[index];
		_vertexElement(out, vertex);
		out += 3;

		if (normals)
		{
			_normalElement(out, vertex);
			out += 3;
		}

		if (glw_state->colorArrayState)
		{
			if (glw_state->colorPointer == tess.svars.colors)
			{
				*out = tess.svars.colors[vertex];
			}
			else
			{
				_colorElement(out, vertex);
			}
		}
		else
		{
			*out = glw_state->currentColor;
		}
		++out;

		if (tex0Layout)
		{
			if (tex0Array)
			{
				_texCoordElement(out, vertex, 0);
			}
			else
			{
				memcpy(out, s_immediateTexCoord[0],
					sizeof(s_immediateTexCoord[0]));
			}
			out += 2;
		}

		if (tex1Array)
		{
			_texCoordElement(out, vertex, 1);
			out += 2;
		}
	}

	if (out != s_nativeVertices + count * glw_state->drawStride)
	{
		g_SPXBPhaseLast = 0x44494635; /* 'DIF5' */
		Com_Error(ERR_FATAL, "Native D3D8 indexed packing mismatch");
		glw_state->inDrawBlock = false;
		return;
	}
	g_SPXBNativeDrawMode = glw_state->primitiveMode;
	g_SPXBNativeDrawCount = count;
	g_SPXBNativeDrawSourceVertices = sourceVertexCount;
	g_SPXBNativeDrawMaxIndex = sourceVertexCount > 0 ? sourceVertexCount - 1 : 0;
	g_SPXBNativeDrawStride = glw_state->drawStride * sizeof(DWORD);
	g_SPXBNativeDrawIndicesPtr = 0;
	g_SPXBNativeDrawVerticesPtr = (unsigned int)s_nativeVertices;
	g_SPXBNativeDrawPath =
		(r_nativeDrawPath ? (unsigned int)r_nativeDrawPath->integer :
			0xffffffffu) |
		(s_nativePushRejectReason << 8);
	if (!SubmitNativeTriangleListPush(s_nativeVertices, count,
			glw_state->drawStride * sizeof(DWORD)))
	{
		Com_Error(ERR_FATAL,
			"Native D3D8 expanded triangle submission failed");
	}
	glw_state->inDrawBlock = false;
}

static int PushDrawIndices(GLsizei count, const GLushort *indices)
{
	const int maxBatchIndices = 1020;
	int remaining = count;
	int sourceIndex = 0;
	int wordsWritten = 0;

	while (remaining > 0)
	{
		int batch = remaining > maxBatchIndices ? maxBatchIndices : remaining;
		const int pairedIndices = batch & ~1;

		glw_state->drawArray =
			_restartIndexPacket(glw_state->drawArray, pairedIndices);
		wordsWritten += 3;

		if (pairedIndices > 0)
		{
			memcpy(glw_state->drawArray, indices + sourceIndex,
				pairedIndices * sizeof(GLushort));
			glw_state->drawArray += pairedIndices / 2;
			wordsWritten += pairedIndices / 2;
		}

		if (batch & 1)
		{
			*glw_state->drawArray++ =
				D3DPUSH_ENCODE(CMD_DRAW_INDEX_LAST, 1);
			*glw_state->drawArray++ = indices[sourceIndex + batch - 1];
			wordsWritten += 2;
		}

		sourceIndex += batch;
		remaining -= batch;
	}

	return wordsWritten;
}

static int DrawIndexPacketWords(GLsizei count)
{
	const int maxBatchIndices = 1020;
	int remaining = count;
	int words = 0;

	while (remaining > 0)
	{
		const int batch =
			remaining > maxBatchIndices ? maxBatchIndices : remaining;
		words += 3 + (batch / 2) + ((batch & 1) ? 2 : 0);
		remaining -= batch;
	}
	return words;
}

static bool CanUseDrawElementsPush(GLenum mode, GLsizei count,
	GLenum type, const GLvoid *indices)
{
	const GLushort *drawIndices;
	int i;

	s_nativePushRejectReason = 0;
	if (mode != GL_TRIANGLES || type != GL_UNSIGNED_SHORT ||
		count <= 0 || (count % 3) != 0 || !indices)
	{
		s_nativePushRejectReason = 1;
		return false;
	}
	if (tess.numVertexes <= 0 || tess.numVertexes > SHADER_MAX_VERTEXES)
	{
		s_nativePushRejectReason = 2;
		return false;
	}
	if (!glw_state->vertexArrayState ||
		glw_state->vertexPointer != tess.xyz)
	{
		s_nativePushRejectReason = 3;
		return false;
	}
	if (glw_state->normalArrayState &&
		glw_state->normalPointer != tess.normal)
	{
		s_nativePushRejectReason = 4;
		return false;
	}
	if (glw_state->colorArrayState && !glw_state->colorPointer)
	{
		s_nativePushRejectReason = 5;
		return false;
	}
	if (glw_state->texCoordArrayState[0] &&
		glw_state->texCoordPointer[0] != tess.svars.texcoords[0])
	{
		s_nativePushRejectReason = 6;
		return false;
	}
	if (glw_state->texCoordArrayState[1] &&
		glw_state->texCoordPointer[1] != tess.svars.texcoords[1])
	{
		s_nativePushRejectReason = 7;
		return false;
	}

	drawIndices = (const GLushort *)indices;
	for (i = 0; i < count; ++i)
	{
		if (drawIndices[i] >= tess.numVertexes)
		{
			s_nativePushRejectReason = 8;
			return false;
		}
	}
	return true;
}

static void ReleaseNativeDrawRing(void)
{
	if (glw_state && glw_state->device)
	{
		glw_state->device->SetStreamSource(0, NULL, 0);
		glw_state->device->SetIndices(NULL, 0);
	}

	if (s_nativeIndexRing)
	{
		s_nativeIndexRing->BlockUntilNotBusy();
		s_nativeIndexRing->Release();
		s_nativeIndexRing = NULL;
	}
	if (s_nativeVertexRing)
	{
		s_nativeVertexRing->BlockUntilNotBusy();
		s_nativeVertexRing->Release();
		s_nativeVertexRing = NULL;
	}
	s_nativeVertexRingOffset = 0;
	s_nativeIndexRingOffset = 0;
}

static bool EnsureNativeVertexRing(void)
{
	HRESULT hr;

	if (s_nativeVertexRing)
		return true;
	if (!glw_state || !glw_state->device)
		return false;

	g_SPXBPhaseLast = 0x44524230; /* 'DRB0' */
	hr = glw_state->device->CreateVertexBuffer(
		STEFX_NATIVE_RING_VERTEX_BYTES, 0, 0, D3DPOOL_DEFAULT,
		&s_nativeVertexRing);
	if (FAILED(hr) || !s_nativeVertexRing)
	{
		XBLF("Native D3D8 vertex ring create failed: bytes=%u hr=0x%08x",
			(unsigned)STEFX_NATIVE_RING_VERTEX_BYTES, (unsigned)hr);
		return false;
	}

	XBLF("Native D3D8 vertex ring ready: vb=%u",
		(unsigned)STEFX_NATIVE_RING_VERTEX_BYTES);
	g_SPXBPhaseLast = 0x44524231; /* 'DRB1' */
	return true;
}

static bool EnsureNativeDrawRing(void)
{
	HRESULT hr;

	if (s_nativeVertexRing && s_nativeIndexRing)
		return true;
	if (!EnsureNativeVertexRing())
		return false;

	hr = glw_state->device->CreateIndexBuffer(
		STEFX_NATIVE_RING_INDEX_BYTES, 0, D3DFMT_INDEX16,
		D3DPOOL_DEFAULT, &s_nativeIndexRing);
	if (FAILED(hr) || !s_nativeIndexRing)
	{
		XBLF("Native D3D8 index ring create failed: bytes=%u hr=0x%08x",
			(unsigned)STEFX_NATIVE_RING_INDEX_BYTES, (unsigned)hr);
		return false;
	}

	XBLF("Native D3D8 draw ring ready: vb=%u ib=%u",
		(unsigned)STEFX_NATIVE_RING_VERTEX_BYTES,
		(unsigned)STEFX_NATIVE_RING_INDEX_BYTES);
	g_SPXBPhaseLast = 0x44524231; /* 'DRB1' */
	return true;
}

static bool SubmitNativeVerticesRing(D3DPRIMITIVETYPE primitiveMode,
	const DWORD *vertices, DWORD vertexCount, DWORD strideBytes)
{
	DWORD vertexBytes;
	DWORD vertexOffset;
	DWORD vertexLockFlags;
	BYTE *vertexDestination = NULL;
	HRESULT hr;

	if (!vertices || vertexCount == 0 || strideBytes == 0)
	{
		g_SPXBPhaseLast = 0x44564630; /* 'DVF0' */
		return vertexCount == 0;
	}

	vertexBytes = vertexCount * strideBytes;
	if (vertexBytes > STEFX_NATIVE_RING_VERTEX_BYTES)
	{
		g_SPXBPhaseLast = 0x44564631; /* 'DVF1' */
		return false;
	}
	if (!EnsureNativeVertexRing())
	{
		if (g_SPXBPhaseLast != 0x44524230)
			g_SPXBPhaseLast = 0x44564632; /* 'DVF2' */
		return false;
	}

	vertexOffset = ((s_nativeVertexRingOffset + strideBytes - 1) /
		strideBytes) * strideBytes;
	vertexLockFlags = vertexOffset == 0 ? 0 : D3DLOCK_NOOVERWRITE;
	if (vertexOffset + vertexBytes > STEFX_NATIVE_RING_VERTEX_BYTES)
	{
		g_SPXBPhaseLast = 0x44565730; /* 'DVW0' */
		s_nativeVertexRing->BlockUntilNotBusy();
		g_SPXBPhaseLast = 0x44565731; /* 'DVW1' */
		vertexOffset = 0;
		vertexLockFlags = 0;
		++g_SPXBNativeRingWraps;
	}

	g_SPXBPhaseLast = 0x44564c30; /* 'DVL0' */
	hr = s_nativeVertexRing->Lock(vertexOffset, vertexBytes,
		&vertexDestination, vertexLockFlags);
	if (FAILED(hr) || !vertexDestination)
	{
		XBLF("Native D3D8 vertex-only ring lock failed: off=%u bytes=%u hr=0x%08x",
			(unsigned)vertexOffset, (unsigned)vertexBytes, (unsigned)hr);
		return false;
	}
	memcpy(vertexDestination, vertices, vertexBytes);
	s_nativeVertexRing->Unlock();

	hr = glw_state->device->SetIndices(NULL, 0);
	if (FAILED(hr))
	{
		g_SPXBPhaseLast = 0x44564930; /* 'DVI0' */
		return false;
	}
	hr = glw_state->device->SetStreamSource(0, s_nativeVertexRing,
		strideBytes);
	if (FAILED(hr))
	{
		g_SPXBPhaseLast = 0x44565330; /* 'DVS0' */
		return false;
	}

	g_SPXBPhaseLast = 0x44564430; /* 'DVD0' */
	hr = glw_state->device->DrawVertices(primitiveMode,
		vertexOffset / strideBytes, vertexCount);
	if (FAILED(hr))
	{
		XBLF("Native D3D8 vertex-only ring draw failed: mode=%u verts=%u hr=0x%08x",
			(unsigned)primitiveMode, (unsigned)vertexCount, (unsigned)hr);
		return false;
	}

	s_nativeVertexRingOffset = vertexOffset + vertexBytes;
	++g_SPXBFakeGLPrimitiveCalls;
	g_SPXBFakeGLPrimitiveVerts += vertexCount;
	++g_SPXBNativeRingCalls;
	g_SPXBNativeRingBytes += vertexBytes;
	g_SPXBPhaseLast = 0x44564431; /* 'DVD1' */
	return true;
}

static bool SubmitNativeTriangleListPush(const DWORD *vertices,
	DWORD vertexCount, DWORD strideBytes)
{
	const DWORD strideDwords = strideBytes / sizeof(DWORD);
	DWORD maxBatchVertices;
	DWORD sourceVertex = 0;

	if (!vertices || vertexCount == 0 || (vertexCount % 3) != 0 ||
		strideBytes == 0 || (strideBytes % sizeof(DWORD)) != 0 ||
		glw_state->primitiveMode != D3DPT_TRIANGLELIST)
		return false;

	maxBatchVertices = (GLW_MAX_DRAW_PACKET_SIZE - 5) / strideDwords;
	maxBatchVertices -= maxBatchVertices % 3;
	if (maxBatchVertices < 3)
		return false;

	g_SPXBPhaseLast = 0x44565359; /* 'DVSY' */
	glw_state->device->KickPushBuffer();
	glw_state->device->BlockUntilIdle();
	g_SPXBPhaseLast = 0x4456535a; /* 'DVSZ' */

	while (sourceVertex < vertexCount)
	{
		DWORD batchVertices = vertexCount - sourceVertex;
		DWORD commandWords;
		DWORD *pushStart = NULL;
		DWORD *push;

		if (batchVertices > maxBatchVertices)
			batchVertices = maxBatchVertices;
		commandWords = 3 + batchVertices * strideDwords + 2;

		g_SPXBPhaseLast = 0x44565030; /* 'DVP0' */
		glw_state->device->BeginPush(commandWords, &pushStart);
		if (!pushStart)
			return false;

		push = _restartDrawPacket(pushStart, batchVertices);
		memcpy(push, vertices + sourceVertex * strideDwords,
			batchVertices * strideBytes);
		push += batchVertices * strideDwords;
		push = _terminateDrawPacket(push);
		if (push != pushStart + commandWords)
		{
			Com_Error(ERR_FATAL,
				"Native D3D8 triangle push size mismatch: reserved=%u wrote=%u",
				(unsigned)commandWords, (unsigned)(push - pushStart));
			return false;
		}

		glw_state->device->EndPush(push);
		sourceVertex += batchVertices;
		++g_SPXBFakeGLPrimitiveCalls;
		g_SPXBFakeGLPrimitiveVerts += batchVertices;
		++g_SPXBNativePushCalls;
		g_SPXBNativePushBytes += commandWords * sizeof(DWORD);
		g_SPXBPhaseLast = 0x44565031; /* 'DVP1' */
	}

	return true;
}

/*
=================
dllDrawElementsRing

Correctness-oriented dynamic VB/IB ring path based on the XDK's supported
Lock/NOOVERWRITE/DrawIndexedPrimitive flow. It remains opt-in until measured
against the UP and direct-push paths on the same map.
=================
*/
static bool dllDrawElementsRing(GLenum mode, GLsizei count, GLenum type,
	const GLvoid *indices)
{
	static DWORD s_ringVertices[
		SHADER_MAX_VERTEXES * STEFX_MAX_NATIVE_VERTEX_DWORDS];
	const bool normals = glw_state->normalArrayState != 0;
	const bool tex0Array = glw_state->texCoordArrayState[0] != 0;
	const bool tex1Array = glw_state->texCoordArrayState[1] != 0;
	const bool tex0Layout = tex0Array || tex1Array;
	const DWORD indexBytes = count * sizeof(GLushort);
	const GLushort *sourceIndices = (const GLushort *)indices;
	DWORD *out = s_ringVertices;
	DWORD vertexBytes;
	DWORD vertexOffset;
	DWORD indexOffset;
	DWORD vertexLockFlags = D3DLOCK_NOOVERWRITE;
	DWORD indexLockFlags = D3DLOCK_NOOVERWRITE;
	BYTE *vertexDestination = NULL;
	BYTE *indexDestination = NULL;
	HRESULT hr;
	int vertex;
	int index;
	GLushort minIndex = 0xffff;
	GLushort maxIndex = 0;

	if (mode != GL_TRIANGLES || type != GL_UNSIGNED_SHORT ||
		!sourceIndices || count <= 0 || (count % 3) != 0)
		return false;

	glw_state->inDrawBlock = true;
	glw_state->primitiveMode = D3DPT_TRIANGLELIST;
	_updateDrawStride(normals, tex0Layout, tex1Array);
	if (glw_state->drawStride <= 0 ||
		glw_state->drawStride > STEFX_MAX_NATIVE_VERTEX_DWORDS)
	{
		glw_state->inDrawBlock = false;
		return false;
	}
	_updateShader(normals, tex0Layout, tex1Array);
	_updateTextures();
	_updateMatrices();

	for (vertex = 0; vertex < tess.numVertexes; ++vertex)
	{
		_vertexElement(out, vertex);
		out += 3;

		if (normals)
		{
			_normalElement(out, vertex);
			out += 3;
		}

		if (glw_state->colorArrayState)
		{
			if (glw_state->colorPointer == tess.svars.colors)
				*out = tess.svars.colors[vertex];
			else
				_colorElement(out, vertex);
		}
		else
		{
			*out = glw_state->currentColor;
		}
		++out;

		if (tex0Layout)
		{
			if (tex0Array)
			{
				_texCoordElement(out, vertex, 0);
			}
			else
			{
				memcpy(out, s_immediateTexCoord[0],
					sizeof(s_immediateTexCoord[0]));
			}
			out += 2;
		}

		if (tex1Array)
		{
			_texCoordElement(out, vertex, 1);
			out += 2;
		}
	}

	vertexBytes = tess.numVertexes * glw_state->drawStride * sizeof(DWORD);
	if (out != s_ringVertices +
			tess.numVertexes * glw_state->drawStride ||
		vertexBytes > STEFX_NATIVE_RING_VERTEX_BYTES ||
		indexBytes > STEFX_NATIVE_RING_INDEX_BYTES ||
		!EnsureNativeDrawRing())
	{
		glw_state->inDrawBlock = false;
		return false;
	}

	vertexOffset = ((s_nativeVertexRingOffset + glw_state->drawStride *
		sizeof(DWORD) - 1) /
		(glw_state->drawStride * sizeof(DWORD))) *
		(glw_state->drawStride * sizeof(DWORD));
	if (vertexOffset + vertexBytes > STEFX_NATIVE_RING_VERTEX_BYTES)
	{
		g_SPXBPhaseLast = 0x44525630; /* 'DRV0' */
		s_nativeVertexRing->BlockUntilNotBusy();
		g_SPXBPhaseLast = 0x44525631; /* 'DRV1' */
		vertexOffset = 0;
		vertexLockFlags = 0;
		++g_SPXBNativeRingWraps;
	}

	indexOffset = (s_nativeIndexRingOffset + 1) & ~1;
	if (indexOffset + indexBytes > STEFX_NATIVE_RING_INDEX_BYTES)
	{
		g_SPXBPhaseLast = 0x44524930; /* 'DRI0' */
		s_nativeIndexRing->BlockUntilNotBusy();
		g_SPXBPhaseLast = 0x44524931; /* 'DRI1' */
		indexOffset = 0;
		indexLockFlags = 0;
		++g_SPXBNativeRingWraps;
	}

	for (index = 0; index < count; ++index)
	{
		if (sourceIndices[index] < minIndex)
			minIndex = sourceIndices[index];
		if (sourceIndices[index] > maxIndex)
			maxIndex = sourceIndices[index];
	}

	g_SPXBNativeDrawMode = mode;
	g_SPXBNativeDrawCount = count;
	g_SPXBNativeDrawSourceVertices = tess.numVertexes;
	g_SPXBNativeDrawMaxIndex = maxIndex;
	g_SPXBNativeDrawStride = glw_state->drawStride * sizeof(DWORD);
	g_SPXBNativeDrawIndicesPtr = (unsigned int)sourceIndices;
	g_SPXBNativeDrawVerticesPtr = (unsigned int)s_ringVertices;
	g_SPXBNativeDrawPath = 2;
	g_SPXBNativeDrawShader = glw_state->shaderMask;
	g_SPXBNativeDrawVertexOffset = vertexOffset;
	g_SPXBNativeDrawIndexOffset = indexOffset;
	g_SPXBNativeDrawVertexBytes = vertexBytes;
	g_SPXBNativeDrawIndexBytes = indexBytes;
	g_SPXBNativeDrawLockFlags =
		(vertexLockFlags & 0xffff) | (indexLockFlags << 16);
	g_SPXBNativeDrawMinIndex = minIndex;

	g_SPXBPhaseLast = 0x44524c30; /* 'DRL0' */
	hr = s_nativeVertexRing->Lock(vertexOffset, vertexBytes,
		&vertexDestination, vertexLockFlags);
	if (FAILED(hr) || !vertexDestination)
	{
		XBLF("Native D3D8 vertex ring lock failed: off=%u bytes=%u hr=0x%08x",
			(unsigned)vertexOffset, (unsigned)vertexBytes, (unsigned)hr);
		glw_state->inDrawBlock = false;
		return false;
	}
	memcpy(vertexDestination, s_ringVertices, vertexBytes);
	s_nativeVertexRing->Unlock();

	hr = s_nativeIndexRing->Lock(indexOffset, indexBytes,
		&indexDestination, indexLockFlags);
	if (FAILED(hr) || !indexDestination)
	{
		XBLF("Native D3D8 index ring lock failed: off=%u bytes=%u hr=0x%08x",
			(unsigned)indexOffset, (unsigned)indexBytes, (unsigned)hr);
		glw_state->inDrawBlock = false;
		return false;
	}
	memcpy(indexDestination, sourceIndices, indexBytes);
	s_nativeIndexRing->Unlock();

	hr = glw_state->device->SetStreamSource(0, s_nativeVertexRing,
		glw_state->drawStride * sizeof(DWORD));
	if (FAILED(hr))
	{
		glw_state->inDrawBlock = false;
		return false;
	}
	hr = glw_state->device->SetIndices(s_nativeIndexRing,
		vertexOffset / (glw_state->drawStride * sizeof(DWORD)));
	if (FAILED(hr))
	{
		glw_state->inDrawBlock = false;
		return false;
	}

	g_SPXBPhaseLast = 0x44524430; /* 'DRD0' */
	hr = glw_state->device->DrawIndexedPrimitive(
		D3DPT_TRIANGLELIST, 0, tess.numVertexes,
		indexOffset / sizeof(GLushort), count / 3);
	if (FAILED(hr))
	{
		XBLF("Native D3D8 ring draw failed: verts=%d indices=%d hr=0x%08x",
			tess.numVertexes, count, (unsigned)hr);
		glw_state->inDrawBlock = false;
		return false;
	}

	s_nativeVertexRingOffset = vertexOffset + vertexBytes;
	s_nativeIndexRingOffset = indexOffset + indexBytes;
	++g_SPXBFakeGLPrimitiveCalls;
	g_SPXBFakeGLPrimitiveVerts += count;
	++g_SPXBNativeRingCalls;
	g_SPXBNativeRingBytes += vertexBytes + indexBytes;
	g_SPXBPhaseLast = 0x44524431; /* 'DRD1' */
	glw_state->inDrawBlock = false;
	return true;
}

/*
=================
dllDrawElementsPush

Write tessellator streams and triangle-list indices directly into one Xbox
pushbuffer. Every pass is self-contained: pointers into a completed pushbuffer
are transient on both the XDK and HLE implementations and must never be reused.
The caller only selects this path for the exact tess-owned layout below.
=================
*/
static void dllDrawElementsPush(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
	int normals, tex0, tex1, v;

	assert(type == GL_UNSIGNED_SHORT);

	normals = glw_state->normalArrayState ? tess.numVertexes : 0;
	tex0 = glw_state->texCoordArrayState[0] ? tess.numVertexes : 0;
	tex1 = glw_state->texCoordArrayState[1] ? tess.numVertexes : 0;

	// start the draw block
	glw_state->inDrawBlock = true;
	glw_state->primitiveMode = _convertPrimMode(mode);

	// update DX with any pending state changes
	_updateDrawStride(normals, tex0, tex1);
	glw_state->drawStride += normals ? 2 : 1;
	_updateShader(normals, tex0, tex1);
	_updateTextures();
	_updateMatrices();

	glw_state->numIndices = 0;
	glw_state->totalIndices = count;
	glw_state->maxIndices = _getMaxIndices();

	glw_state->device->SetStreamSource(0, NULL, glw_state->drawStride * 4);

	const int vert_size = glw_state->drawStride * tess.numVertexes;
	const int index_words = DrawIndexPacketWords(count);
	const int stream_command_words =
		2 + (normals ? 2 : 0) + 2 + (tex0 ? 2 : 0) + (tex1 ? 2 : 0);
	const int command_words =
		1 + vert_size + 17 + stream_command_words + index_words + 2;
	DWORD *pushStart = NULL;

	g_SPXBPhaseLast = 0x44495030; /* 'DIP0' */
	glw_state->device->BeginPush(command_words, &glw_state->drawArray);
	pushStart = glw_state->drawArray;
	if (!pushStart)
	{
		g_SPXBPhaseLast = 0x44495046; /* 'DIPF' */
		Com_Error(ERR_FATAL,
			"Native D3D8 BeginPush failed: words=%d verts=%d indices=%d",
			command_words, tess.numVertexes, count);
		glw_state->inDrawBlock = false;
		return;
	}

	DWORD *jumpaddress = 0, *stream = 0;

	// Keep all vertex streams inside this BeginPush/EndPush lifetime.
	jumpaddress = glw_state->drawArray + vert_size + 1;
	*glw_state->drawArray++ = ((DWORD)jumpaddress & 0x7fffffff) | 1;
	stream = glw_state->drawArray;

	memcpy(glw_state->drawArray, tess.xyz,
		sizeof(vec4_t) * tess.numVertexes);
	glw_state->drawArray += tess.numVertexes * 4;

	if(normals)
	{
		memcpy(glw_state->drawArray, tess.normal,
			sizeof(vec4_t) * tess.numVertexes);
		glw_state->drawArray += tess.numVertexes * 4;
	}

	if(glw_state->colorArrayState)
	{
		if (glw_state->colorPointer == tess.svars.colors)
		{
			memcpy(glw_state->drawArray, tess.svars.colors,
				sizeof(D3DCOLOR) * tess.numVertexes);
		}
		else
		{
			for (v = 0; v < tess.numVertexes; ++v)
				_colorElement(glw_state->drawArray + v, v);
		}
	}
	else
	{
		for (v = 0; v < tess.numVertexes; ++v)
			glw_state->drawArray[v] = glw_state->currentColor;
	}
	glw_state->drawArray += tess.numVertexes;

	if(tex0)
	{
		memcpy(glw_state->drawArray, tess.svars.texcoords[0],
			sizeof(vec2_t) * tess.numVertexes);
		glw_state->drawArray += tess.numVertexes * 2;
	}

	if(tex1)
	{
		memcpy(glw_state->drawArray, tess.svars.texcoords[1],
			sizeof(vec2_t) * tess.numVertexes);
		glw_state->drawArray += tess.numVertexes * 2;
	}

	// Write the vertex shader
#define CMD_STREAM_STRIDEANDTYPE0 0x1760
	*glw_state->drawArray++ = D3DPUSH_ENCODE(CMD_STREAM_STRIDEANDTYPE0, 16);
	*glw_state->drawArray++ = (16 << 8)|D3DVSDT_FLOAT3;

	if(1)
	{
		*glw_state->drawArray++ = ((glw_state->drawStride * 4) << 8) | D3DVSDT_NONE;
	}

	if(normals)
	{
		*glw_state->drawArray++ = (16 << 8) | D3DVSDT_FLOAT3;
	}
	else
	{
		*glw_state->drawArray++ = ((glw_state->drawStride * 4) << 8) | D3DVSDT_NONE;
	}

	*glw_state->drawArray++ = (4 << 8) | D3DVSDT_D3DCOLOR;

	for(int i = 0; i < 5; i++)
	{
		*glw_state->drawArray++ = ((glw_state->drawStride * 4) << 8) | D3DVSDT_NONE;
	}

	if(tex0)
	{
		*glw_state->drawArray++ = (8 << 8) | D3DVSDT_FLOAT2;
	}
	else
	{
		*glw_state->drawArray++ = ((glw_state->drawStride * 4) << 8) | D3DVSDT_NONE;
	}

	if(tex1)
	{
		*glw_state->drawArray++ = (8 << 8) | D3DVSDT_FLOAT2;
	}
	else
	{
		*glw_state->drawArray++ = ((glw_state->drawStride * 4) << 8) | D3DVSDT_NONE;
	}

	for ( int i = 0; i < 5; i++)
	{
		*glw_state->drawArray++ = ((glw_state->drawStride * 4) << 8) | D3DVSDT_NONE;
	}

//	 Write the indicator to our vertex stream
#define CMD_VERTEXSTREAM_XYZ		0x1720
#define CMD_VERTEXSTREAM_NORMAL		0x1728
#define CMD_VERTEXSTREAM_COLOR		0x172c
#define CMD_VERTEXSTREAM_TEX0		0x1744
#define CMD_VERTEXSTREAM_TEX1		0x1748

	// Bind the streams copied into this pushbuffer.
	*glw_state->drawArray++ = D3DPUSH_ENCODE(CMD_VERTEXSTREAM_XYZ, 1);
	*glw_state->drawArray++ = (DWORD)stream & 0x7fffffff;
	stream += tess.numVertexes * 4;

	if(normals)
	{
		*glw_state->drawArray++ = D3DPUSH_ENCODE(CMD_VERTEXSTREAM_NORMAL, 1);
		*glw_state->drawArray++ = (DWORD)stream & 0x7fffffff;
		stream += tess.numVertexes * 4;
	}

	*glw_state->drawArray++ = D3DPUSH_ENCODE(CMD_VERTEXSTREAM_COLOR, 1);
	*glw_state->drawArray++ = (DWORD)stream & 0x7fffffff;
	stream += tess.numVertexes;

	if(tex0)
	{
		*glw_state->drawArray++ = D3DPUSH_ENCODE(CMD_VERTEXSTREAM_TEX0, 1);
		*glw_state->drawArray++ = (DWORD)stream & 0x7fffffff;
		stream += tess.numVertexes * 2;
	}

	if(tex1)
	{
		*glw_state->drawArray++ = D3DPUSH_ENCODE(CMD_VERTEXSTREAM_TEX1, 1);
		*glw_state->drawArray++ = (DWORD)stream & 0x7fffffff;
	}

	// Send thru the index data
	PushDrawIndices(count, (const GLushort *)indices);

	// finish up the draw
	glw_state->inDrawBlock = false;

	DWORD* push = _terminateIndexPacket(glw_state->drawArray);

	if (push != pushStart + command_words)
	{
		g_SPXBPhaseLast = 0x44495053; /* 'DIPS' */
		Com_Error(ERR_FATAL,
			"Native D3D8 push size mismatch: reserved=%d wrote=%d",
			command_words, (int)(push - pushStart));
		return;
	}
	glw_state->device->EndPush(push);
	++g_SPXBFakeGLPrimitiveCalls;
	g_SPXBFakeGLPrimitiveVerts += count;
	++g_SPXBNativePushCalls;
	g_SPXBNativePushBytes += command_words * sizeof(DWORD);
	g_SPXBPhaseLast = 0x44495031; /* 'DIP1' */
}

// NOTE: This is a core draw routine. It must remain on a native D3D8 path.
static void dllDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
	if (r_nativeDrawPath && r_nativeDrawPath->integer == 2)
	{
		if (CanUseDrawElementsPush(mode, count, type, indices) &&
			dllDrawElementsRing(mode, count, type, indices))
		{
			return;
		}
		++g_SPXBNativeRingFallbacks;
	}
	if (r_nativeDrawPath && r_nativeDrawPath->integer == 1)
	{
		if (CanUseDrawElementsPush(mode, count, type, indices))
		{
			dllDrawElementsPush(mode, count, type, indices);
			return;
		}
		++g_SPXBNativePushFallbacks;
	}
	dllDrawElementsUP(mode, count, type, indices);
}

static void dllDrawPixels(GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels)
{
	assert(0);
}

static void dllEdgeFlag(GLboolean flag)
{
	assert(0);
}

static void dllEdgeFlagPointer(GLsizei stride, const GLvoid *pointer)
{
	assert(0);
}

static void dllEdgeFlagv(const GLboolean *flag)
{
	assert(0);
}

static void dllEnable(GLenum cap)
{
	setCap(cap, true);
}

static void dllEnableClientState(GLenum array)
{
	assert(!glw_state->inDrawBlock);
	setArrayState(array, true);
}

static void dllEnd(void)
{
	assert(glw_state->inDrawBlock);
	if (!glw_state || !glw_state->device || !glw_state->inDrawBlock ||
		!glw_state->drawArray)
	{
		g_SPXBPhaseLast = 0x494D4632; /* 'IMF2' */
		Com_Error(ERR_DROP, "Native D3D8 immediate draw ended in an invalid state");
		return;
	}
	glw_state->inDrawBlock = false;

	if (s_immediateUsesUP)
	{
		if (glw_state->numVertices > 0)
		{
			if (!SubmitNativeVerticesRing(glw_state->primitiveMode,
					s_immediateVertices, glw_state->numVertices,
					glw_state->drawStride * sizeof(DWORD)))
			{
				Com_Error(ERR_FATAL,
					"Native D3D8 immediate vertex-ring submission failed");
			}
		}
		glw_state->drawArray = NULL;
		s_immediateUsesUP = false;
		return;
	}

	assert(false);
}

#if YELLOW_MODE

static DWORD YellowModePixelShader	 = -1;
bool enableYellowMode	= false;
static void initYellowMode( void )
{
	if(!(CreatePixelShader("D:\\base\\media\\yellow.xpu", &YellowModePixelShader)))
		return;
}

static void renderYellowMode( void )
{
	if(!enableYellowMode || YellowModePixelShader == -1)
		return;

	if(cls.state == CA_CINEMATIC)
		return;

	if(!tr.screenImage)
		return;

	// bind tr.screenImage
	GL_Bind(tr.screenImage);
	GL_State(0);

	// copy backbuffer
	//qglCopyBackBufferToTexEXT(width, height, u1, v1, u2, v2);
	qglCopyBackBufferToTexEXT(512.0f, 256.0f, 2.0f, 2.0f, 640.0f, 480.0f);

	// set up the yellow mode shader
	DWORD oldShader;
	glw_state->device->GetPixelShader(&oldShader);
	glw_state->device->SetPixelShader(YellowModePixelShader);

	// draw our polygon
	qglBeginEXT(GL_QUADS,4,0,0,4,0);

	qglTexCoord2f( 0,0 );
	qglVertex2f( 0, 0 );

	qglTexCoord2f( 1,0 );
	qglVertex2f( 640, 0 );

	qglTexCoord2f( 1,1 );
	qglVertex2f( 640, 480);

	qglTexCoord2f( 0,1 );
	qglVertex2f( 0, 480);

	qglEnd ();

	glw_state->device->SetPixelShader(oldShader);
}

#endif // YELLOW_MODE

// EXTENSION: End drawing for a frame
bool connectSwapOverride = false;
static void dllEndFrame(void)
{
	assert(!glw_state->inDrawBlock);
	g_SPXBPhaseLast = 0x45463030; /* 'EF00' */

	// the blend state can get reset by Present()...
	GLboolean blend = qglIsEnabled(GL_BLEND);
	
#if YELLOW_MODE
	if( !connectSwapOverride )
		renderYellowMode();
#endif // YELLOW_MODE

	g_SPXBPhaseLast = 0x45463031; /* 'EF01' */
	glw_state->device->EndScene();
	g_SPXBPhaseLast = 0x45463032; /* 'EF02' */
	
	qglViewport(0, 0, glConfig.vidWidth, glConfig.vidHeight);
	if( !connectSwapOverride )
	{
		g_SPXBPhaseLast = 0x45463033; /* 'EF03' */
		glw_state->device->Present(NULL, NULL, NULL, NULL);
		g_SPXBPhaseLast = 0x45463034; /* 'EF04' */
	}

	// restore the pre-Present state
	if (blend) qglEnable(GL_BLEND);
	else qglDisable(GL_BLEND);
	g_SPXBPhaseLast = 0x45463035; /* 'EF05' */
}

// EXTENSION: End shadow draw mode
static void dllEndShadow(void)
{
	//Intentionally left blank
}

static void dllEndList(void)
{
	assert(0);
}

static void dllEvalCoord1d(GLdouble u)
{
	assert(0);
}

static void dllEvalCoord1dv(const GLdouble *u)
{
	assert(0);
}

static void dllEvalCoord1f(GLfloat u)
{
	assert(0);
}

static void dllEvalCoord1fv(const GLfloat *u)
{
	assert(0);
}

static void dllEvalCoord2d(GLdouble u, GLdouble v)
{
	assert(0);
}

static void dllEvalCoord2dv(const GLdouble *u)
{
	assert(0);
}

static void dllEvalCoord2f(GLfloat u, GLfloat v)
{
	assert(0);
}

static void dllEvalCoord2fv(const GLfloat *u)
{
	assert(0);
}

static void dllEvalMesh1(GLenum mode, GLint i1, GLint i2)
{
	assert(0);
}

static void dllEvalMesh2(GLenum mode, GLint i1, GLint i2, GLint j1, GLint j2)
{
	assert(0);
}

static void dllEvalPoint1(GLint i)
{
	assert(0);
}

static void dllEvalPoint2(GLint i, GLint j)
{
	assert(0);
}

static void dllFeedbackBuffer(GLsizei size, GLenum type, GLfloat *buffer)
{
	assert(0);
}

static void dllFinish(void)
{
#ifdef _XBOX
	glw_state->device->BlockUntilIdle();
#endif
}

static void dllFlush(void)
{
#ifdef _XBOX
	glw_state->device->BlockUntilIdle();
#endif
}

// EXTENSION: Draw the shadow
static void dllFlushShadow(void)
{
	//Intentionally left blank
}

static D3DFOGMODE _convertFogMode(GLint param)
{
	switch(param)
	{
	case GL_LINEAR: return D3DFOG_LINEAR; break;
	case GL_EXP: return D3DFOG_EXP; break;
	case GL_EXP2: return D3DFOG_EXP2; break;
	}

	return D3DFOG_NONE;
}

static void dllFogf(GLenum pname, GLfloat param)
{
	assert(pname == GL_FOG_DENSITY || pname == GL_FOG_START || pname == GL_FOG_END);	

	switch(pname)
	{
	case GL_FOG_DENSITY: glw_state->device->SetRenderState( D3DRS_FOGDENSITY, *(DWORD*)&param ); break;
	case GL_FOG_START: glw_state->device->SetRenderState( D3DRS_FOGSTART, *(DWORD*)&param ); break;
	case GL_FOG_END: glw_state->device->SetRenderState( D3DRS_FOGEND, *(DWORD*)&param ); break;
	}
}

static void dllFogfv(GLenum pname, const GLfloat *params)
{
	assert(pname == GL_FOG_COLOR);

	D3DCOLOR color = D3DCOLOR_ARGB(0x00, 
								  (int)(params[0] * 255.0f),
								  (int)(params[1] * 255.0f),
								  (int)(params[2] * 255.0f));

	glw_state->device->SetRenderState( D3DRS_FOGCOLOR, color );
}

static void dllFogi(GLenum pname, GLint param)
{
	assert(pname == GL_FOG_MODE);

	glw_state->device->SetRenderState( D3DRS_FOGTABLEMODE, _convertFogMode(param) );
}

static void dllFogiv(GLenum pname, const GLint *params)
{
	assert(0);
}

static void dllFrontFace(GLenum mode)
{
	assert(0);
}

static void dllFrustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar)
{
	D3DXMATRIX m;
	D3DXMatrixPerspectiveOffCenterRH(&m, left, right, bottom, top, zNear, zFar);
	glw_state->matrixStack[glw_state->matrixMode]->MultMatrix(&m);
	glw_state->matricesDirty[glw_state->matrixMode] = true;
}

GLuint dllGenLists(GLsizei range)
{
	assert(0);
	return 0;
}

static void dllGenTextures(GLsizei n, GLuint *textures)
{
	for (int i = 0; i < n; ++i)
	{
		textures[i] = glw_state->textureBindNum++;
	}
}

// Implemented only the states we use.
template <typename T>
static void _getState(GLenum pname, T *params)
{
	switch (pname)
	{
	case GL_CULL_FACE: params[0] = (T)glw_state->cullEnable; break;
	case GL_MAX_TEXTURE_SIZE: params[0] = (T)512; break;
	case GL_MAX_ACTIVE_TEXTURES_ARB: params[0] = GLW_MAX_TEXTURE_STAGES; break;
	case GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT: params[0] = 4; break;
	default:
		assert(0);
		params[0] = (T)0;
		break;
	}
}

static void dllGetBooleanv(GLenum pname, GLboolean *params)
{
	_getState(pname, params);
}

static void dllGetClipPlane(GLenum plane, GLdouble *equation)
{
	assert(0);
}

static void dllGetDoublev(GLenum pname, GLdouble *params)
{
	_getState(pname, params);
}

GLenum dllGetError(void)
{
	return 0;
}

static void dllGetFloatv(GLenum pname, GLfloat *params)
{
	_getState(pname, params);
}

static void dllGetIntegerv(GLenum pname, GLint *params)
{
	_getState(pname, params);
}

static void dllGetLightfv(GLenum light, GLenum pname, GLfloat *params)
{
	assert(0);
}

static void dllGetLightiv(GLenum light, GLenum pname, GLint *params)
{
	assert(0);
}

static void dllGetMapdv(GLenum target, GLenum query, GLdouble *v)
{
	assert(0);
}

static void dllGetMapfv(GLenum target, GLenum query, GLfloat *v)
{
	assert(0);
}

static void dllGetMapiv(GLenum target, GLenum query, GLint *v)
{
	assert(0);
}

static void dllGetMaterialfv(GLenum face, GLenum pname, GLfloat *params)
{
	assert(0);
}

static void dllGetMaterialiv(GLenum face, GLenum pname, GLint *params)
{
	assert(0);
}

static void dllGetPixelMapfv(GLenum map, GLfloat *values)
{
	assert(0);
}

static void dllGetPixelMapuiv(GLenum map, GLuint *values)
{
	assert(0);
}

static void dllGetPixelMapusv(GLenum map, GLushort *values)
{
	assert(0);
}

static void dllGetPointerv(GLenum pname, GLvoid* *params)
{
	assert(0);
}

static void dllGetPolygonStipple(GLubyte *mask)
{
	assert(0);
}

const GLubyte * dllGetString(GLenum name)
{
	switch (name)
	{
	case GL_VENDOR: return (const unsigned char*)"Vicarious Visions";
	case GL_RENDERER: return (const unsigned char*)"Optimized DX8/OpenGL Layer";
	case GL_VERSION: return (const unsigned char*)"0.1";
	case GL_EXTENSIONS:
		return (const unsigned char*)
			"EXT_texture_env_add GL_ARB_multitexture EXT_texture_filter_anisotropic";
	default: return (const unsigned char*)"";
	}
}

static void dllGetTexEnvfv(GLenum target, GLenum pname, GLfloat *params)
{
	assert(0);
}

static void dllGetTexEnviv(GLenum target, GLenum pname, GLint *params)
{
	assert(0);
}

static void dllGetTexGendv(GLenum coord, GLenum pname, GLdouble *params)
{
	assert(0);
}

static void dllGetTexGenfv(GLenum coord, GLenum pname, GLfloat *params)
{
	assert(0);
}

static void dllGetTexGeniv(GLenum coord, GLenum pname, GLint *params)
{
	assert(0);
}

static void dllGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, GLvoid *pixels)
{
	assert(0);
}

static void dllGetTexLevelParameterfv(GLenum target, GLint level, GLenum pname, GLfloat *params)
{
	assert(0);
}

static void dllGetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint *params)
{
	assert(0);
}

static void dllGetTexParameterfv(GLenum target, GLenum pname, GLfloat *params)
{
	assert(0);
}

static void dllGetTexParameteriv(GLenum target, GLenum pname, GLint *params)
{
	assert(0);
}

static void dllHint(GLenum target, GLenum mode)
{
	assert(0);
}

// Convert an triangle index array (indices) to a 
// triangle strip index array (dest) with primitive 
// length array.
static void buildStrips(GLuint* len, GLsizei* num_lens, GLushort* dest, GLsizei* num_indices, const GLushort* src)
{
	GLushort last[3];

	// prime the strip
	GLsizei cur_index = 0;
	dest[cur_index++] = src[0];
	dest[cur_index++] = src[1];
	dest[cur_index++] = src[2];
	GLuint cur_length = 3;
	GLsizei num_strips = 0;

	GLuint max_length = GLW_MAX_DRAW_PACKET_SIZE / glw_state->drawStride;
	
	last[0] = src[0];
	last[1] = src[1];
	last[2] = src[2];

	qboolean even = qfalse;

	for ( GLsizei i = 3; i < *num_indices; i += 3 )
	{
		// odd numbered triangle in potential strip
		if ( !even )
		{
			// check previous triangle to see if we're continuing a strip
			if ( ( src[i+0] == last[2] ) && ( src[i+1] == last[1] ) &&
				cur_length < max_length )
			{
				++cur_length;
				dest[cur_index++] = src[i+2];
				even = qtrue;
			}
			// otherwise we're done with this strip so finish it and start
			// a new one
			else
			{
				len[num_strips++] = cur_length;
				cur_length = 3;

				dest[cur_index++] = src[i+0];
				dest[cur_index++] = src[i+1];
				dest[cur_index++] = src[i+2];

				even = qfalse;
			}
		}
		else
		{
			// check previous triangle to see if we're continuing a strip
			if ( ( last[2] == src[i+1] ) && ( last[0] == src[i+0] ) &&
				cur_length < max_length )
			{
				++cur_length;
				dest[cur_index++] = src[i+2];
				even = qfalse;
			}
			// otherwise we're done with this strip so finish it and start
			// a new one
			else
			{
				len[num_strips++] = cur_length;
				cur_length = 3;

				dest[cur_index++] = src[i+0];
				dest[cur_index++] = src[i+1];
				dest[cur_index++] = src[i+2];

				even = qfalse;
			}
		}

		// cache the last three vertices
		last[0] = src[i+0];
		last[1] = src[i+1];
		last[2] = src[i+2];
	}

	len[num_strips++] = cur_length;
	*num_lens = num_strips;
	*num_indices = cur_index;

	assert(num_strips <= GLW_MAX_STRIPS);
}

#ifdef _XBOX
void renderObject_Light( int numIndexes, const glIndex_t *indexes )
{
	int i;

	// start the draw mode
	assert(!glw_state->inDrawBlock);

	glw_state->inDrawBlock = true;
	glw_state->primitiveMode = D3DPT_TRIANGLELIST;

	glw_state->drawStride = 14;

	glw_state->numIndices = 0;
	glw_state->totalIndices = numIndexes;
	glw_state->maxIndices = _getMaxIndices();

	glw_state->device->SetStreamSource(0, NULL, glw_state->drawStride * 4);	

	int vert_size;
	if(!tess.pNormal)
		vert_size = 8 * tess.numVertexes;
	else
		vert_size = 4 * tess.numVertexes;

	int index_size = numIndexes / 2;
	
	glw_state->device->BeginPush(vert_size + index_size + 60, &glw_state->drawArray);

	DWORD *jumpaddress = 0, *stream = 0;

	// Determine where the end of the vertex data is gonna be,
	// that's where we're going to jump to
	jumpaddress = glw_state->drawArray + vert_size + 1;

	// Write the jump address
	*glw_state->drawArray++ = ((DWORD)jumpaddress & 0x7fffffff) | 1;

	// Set up our own fake vertex buffer
	stream = glw_state->drawArray;

	if(!tess.pNormal)
	{
        memcpy(glw_state->drawArray, tess.normal, sizeof(vec4_t) * tess.numVertexes);
		glw_state->drawArray += tess.numVertexes * 4;
	}

	memcpy(glw_state->drawArray, tess.tangent, sizeof(vec4_t) * tess.numVertexes);
	glw_state->drawArray += tess.numVertexes * 4;

	// Write the vertex shader
#define CMD_STREAM_STRIDEANDTYPE0 0x1760
	*glw_state->drawArray++ = D3DPUSH_ENCODE(CMD_STREAM_STRIDEANDTYPE0, 16);

	// Position
	*glw_state->drawArray++ = (16 << 8)|D3DVSDT_FLOAT3;

	// Normal
	*glw_state->drawArray++ = (16 << 8) | D3DVSDT_FLOAT3;

	// Tex Coord
	*glw_state->drawArray++ = (8 << 8) | D3DVSDT_FLOAT2;

	// Tangent
	*glw_state->drawArray++ = (16 << 8) | D3DVSDT_FLOAT3;

	for ( int i = 0; i < 12; i++)
	{
		*glw_state->drawArray++ = ((glw_state->drawStride * 4) << 8) | D3DVSDT_NONE;
	}

	//	 Write the indicator to our vertex stream
	*glw_state->drawArray++ = D3DPUSH_ENCODE(0x1720, 1);
	*glw_state->drawArray++ = (DWORD)tess.pXyz & 0x7fffffff;

	*glw_state->drawArray++ = D3DPUSH_ENCODE(0x1724, 1);
	if(!tess.pNormal)
	{
        *glw_state->drawArray++ = (DWORD)stream & 0x7fffffff;
		stream += tess.numVertexes * 4;
	}
	else
		*glw_state->drawArray++ = (DWORD)tess.pNormal & 0x7fffffff;

	*glw_state->drawArray++ = D3DPUSH_ENCODE(0x1728, 1);
	*glw_state->drawArray++ = (DWORD)tess.pTex1 & 0x7fffffff;

	*glw_state->drawArray++ = D3DPUSH_ENCODE(0x172c, 1);
	*glw_state->drawArray++ = (DWORD)stream & 0x7fffffff;

	// Send thru the index data
	PushIndices(numIndexes, (GLushort*)indexes);

	// finish up the draw
	glw_state->inDrawBlock = false;

	DWORD* push = _terminateIndexPacket(glw_state->drawArray);

	glw_state->device->EndPush(push);
}


void renderObject_Shadow( int primType, int numIndexes, const unsigned short *indexes )
{
	// start the draw mode
	assert(!glw_state->inDrawBlock);
	if (!indexes || numIndexes <= 0 || tess.numVertexes <= 0 ||
		tess.numVertexes > SHADER_MAX_VERTEXES)
	{
		g_SPXBPhaseLast = 0x53484446; /* 'SHDF' */
		Com_Error(ERR_FATAL,
			"Native D3D8 shadow draw invalid: verts=%d indices=%d source=%p",
			tess.numVertexes, numIndexes, indexes);
		return;
	}

	glw_state->inDrawBlock = true;
	glw_state->primitiveMode = (D3DPRIMITIVETYPE)primType;

	glw_state->drawStride = 5;

	glw_state->numIndices = 0;
	glw_state->totalIndices = numIndexes;
	glw_state->maxIndices = _getMaxIndices();

	glw_state->device->SetStreamSource(0, NULL, glw_state->drawStride * 4);	

	int vert_size = glw_state->drawStride * (tess.numVertexes * 2);
	const int index_words = DrawIndexPacketWords(numIndexes);
	const int command_words = 1 + vert_size + 17 + 4 + index_words + 2;
	DWORD *pushStart = NULL;

	g_SPXBPhaseLast = 0x53484430; /* 'SHD0' */
	glw_state->device->BeginPush(command_words, &glw_state->drawArray);
	pushStart = glw_state->drawArray;
	if (!pushStart)
	{
		g_SPXBPhaseLast = 0x53484446; /* 'SHDF' */
		Com_Error(ERR_FATAL,
			"Native D3D8 shadow BeginPush failed: words=%d verts=%d indices=%d",
			command_words, tess.numVertexes, numIndexes);
		glw_state->inDrawBlock = false;
		return;
	}

	DWORD *jumpaddress = 0, *stream = 0;

	// Keep both streams inside this BeginPush/EndPush lifetime.
	jumpaddress = glw_state->drawArray + vert_size + 1;
	*glw_state->drawArray++ = ((DWORD)jumpaddress & 0x7fffffff) | 1;
	stream = glw_state->drawArray;

	memcpy(glw_state->drawArray, tess.xyz,
		sizeof(vec4_t) * (tess.numVertexes * 2));
	glw_state->drawArray += (tess.numVertexes * 2) * 4;

	memset(glw_state->drawArray, 0x0, sizeof(float) * tess.numVertexes);
	glw_state->drawArray += tess.numVertexes;
	memcpy(glw_state->drawArray, StencilShadower.m_extrusionIndicators,
		sizeof(float) * tess.numVertexes);
	glw_state->drawArray += tess.numVertexes;
	
	// Write the vertex shader
#define CMD_STREAM_STRIDEANDTYPE0 0x1760
	*glw_state->drawArray++ = D3DPUSH_ENCODE(CMD_STREAM_STRIDEANDTYPE0, 16);

	// Position
	*glw_state->drawArray++ = (16 << 8)|D3DVSDT_FLOAT3;

	// Extrusion determinant
	*glw_state->drawArray++ = (4 << 8)|D3DVSDT_FLOAT1;

	for ( int i = 0; i < 14; i++)
	{
		*glw_state->drawArray++ = ((glw_state->drawStride * 4) << 8) | D3DVSDT_NONE;
	}

	//	 Write the indicator to our vertex stream
	*glw_state->drawArray++ = D3DPUSH_ENCODE(0x1720, 1);
	*glw_state->drawArray++ = (DWORD)stream & 0x7fffffff;
	stream += (tess.numVertexes * 2) * 4;

	*glw_state->drawArray++ = D3DPUSH_ENCODE(0x1724, 1);
	*glw_state->drawArray++ = (DWORD)stream & 0x7fffffff;

	// Send thru the index data
	PushDrawIndices(numIndexes, (const GLushort *)indexes);

	// finish up the draw
	glw_state->inDrawBlock = false;

	DWORD* push = _terminateIndexPacket(glw_state->drawArray);

	if (push != pushStart + command_words)
	{
		g_SPXBPhaseLast = 0x53484453; /* 'SHDS' */
		Com_Error(ERR_FATAL,
			"Native D3D8 shadow push size mismatch: reserved=%d wrote=%d",
			command_words, (int)(push - pushStart));
		return;
	}
	glw_state->device->EndPush(push);
	++g_SPXBNativePushCalls;
	g_SPXBNativePushBytes += command_words * sizeof(DWORD);
	g_SPXBPhaseLast = 0x53484431; /* 'SHD1' */
}


void renderObject_Bump()
{
	// start the draw mode
	assert(!glw_state->inDrawBlock);

	glw_state->inDrawBlock = true;
	glw_state->primitiveMode = D3DPT_TRIANGLELIST;

	glw_state->drawStride = 16;

	glw_state->numIndices = 0;
	glw_state->totalIndices = tess.numIndexes;
	glw_state->maxIndices = _getMaxIndices();

	glw_state->device->SetStreamSource(0, NULL, glw_state->drawStride * 4);	

	int vert_size = glw_state->drawStride * tess.numVertexes;
	int index_size = tess.numIndexes / 2;

	glw_state->device->BeginPush(vert_size + index_size + 60, &glw_state->drawArray);

	DWORD *jumpaddress = 0, *stream = 0;

	// Determine where the end of the vertex data is gonna be,
	// that's where we're going to jump to
	jumpaddress = glw_state->drawArray + vert_size + 1;

	// Write the jump address
	*glw_state->drawArray++ = ((DWORD)jumpaddress & 0x7fffffff) | 1;

	// Set up our own fake vertex buffer
	stream = glw_state->drawArray;

	memcpy(glw_state->drawArray, tess.xyz, sizeof(vec4_t) * tess.numVertexes);
	glw_state->drawArray += tess.numVertexes * 4;

	memcpy(glw_state->drawArray, tess.normal, sizeof(vec4_t) * tess.numVertexes);
	glw_state->drawArray += tess.numVertexes * 4;

	memcpy(glw_state->drawArray, tess.svars.texcoords[0], sizeof(vec2_t) * tess.numVertexes);
	glw_state->drawArray += tess.numVertexes * 2;

	memcpy(glw_state->drawArray, tess.svars.texcoords[1], sizeof(vec2_t) * tess.numVertexes);
	glw_state->drawArray += tess.numVertexes * 2;

	memcpy(glw_state->drawArray, tess.tangent, sizeof(vec4_t) * tess.numVertexes);
	glw_state->drawArray += tess.numVertexes * 4;

	// Write the vertex shader
#define CMD_STREAM_STRIDEANDTYPE0 0x1760
	*glw_state->drawArray++ = D3DPUSH_ENCODE(CMD_STREAM_STRIDEANDTYPE0, 16);

	// Position
	*glw_state->drawArray++ = (16 << 8)|D3DVSDT_FLOAT3;

	// Normal
	*glw_state->drawArray++ = (16 << 8) | D3DVSDT_FLOAT3;

	// Tex Coord 0
	*glw_state->drawArray++ = (8 << 8) | D3DVSDT_FLOAT2;

	// Tex Coord 1
	*glw_state->drawArray++ = (8 << 8) | D3DVSDT_FLOAT2;

	// Tangent
	*glw_state->drawArray++ = (16 << 8) | D3DVSDT_FLOAT3;

	for(int i = 0; i < 11; i++)
	{
		*glw_state->drawArray++ = ((glw_state->drawStride * 4) << 8) | D3DVSDT_NONE;
	}

	//	 Write the indicator to our vertex stream
	*glw_state->drawArray++ = D3DPUSH_ENCODE(0x1720, 1);
	*glw_state->drawArray++ = (DWORD)stream & 0x7fffffff;
	tess.pXyz = stream;
	stream += tess.numVertexes * 4;

	*glw_state->drawArray++ = D3DPUSH_ENCODE(0x1724, 1);
	*glw_state->drawArray++ = (DWORD)stream & 0x7fffffff;
	tess.pNormal = stream;
	stream += tess.numVertexes * 4;

	*glw_state->drawArray++ = D3DPUSH_ENCODE(0x1728, 1);
	*glw_state->drawArray++ = (DWORD)stream & 0x7fffffff;
	stream += tess.numVertexes * 2;

	*glw_state->drawArray++ = D3DPUSH_ENCODE(0x172c, 1);
	*glw_state->drawArray++ = (DWORD)stream & 0x7fffffff;
	stream += tess.numVertexes * 2;

	*glw_state->drawArray++ = D3DPUSH_ENCODE(0x1730, 1);
	*glw_state->drawArray++ = (DWORD)stream & 0x7fffffff;
	stream += tess.numVertexes * 4;

	// Send thru the index data
	PushIndices(tess.numIndexes, (GLushort*)tess.indexes);

	// finish up the draw
	glw_state->inDrawBlock = false;

	DWORD* push = _terminateIndexPacket(glw_state->drawArray);

	glw_state->device->EndPush(push);
}

void renderObject_Env()
{
	// start the draw mode
	assert(!glw_state->inDrawBlock);

	glw_state->inDrawBlock = true;
	glw_state->primitiveMode = D3DPT_TRIANGLELIST;

	glw_state->drawStride = 9;
	_updateTextures();

	glw_state->numIndices = 0;
	glw_state->totalIndices = tess.numIndexes;
	glw_state->maxIndices = _getMaxIndices();

	glw_state->device->SetStreamSource(0, NULL, glw_state->drawStride * 4);	

	int vert_size = glw_state->drawStride * tess.numVertexes;
	int index_size = tess.numIndexes / 2;

	glw_state->device->BeginPush(vert_size + index_size + 60, &glw_state->drawArray);

	DWORD *jumpaddress = 0, *stream = 0;

	// Determine where the end of the vertex data is gonna be,
	// that's where we're going to jump to
	jumpaddress = glw_state->drawArray + vert_size + 1;

	// Write the jump address
	*glw_state->drawArray++ = ((DWORD)jumpaddress & 0x7fffffff) | 1;

	// Set up our own fake vertex buffer
	stream = glw_state->drawArray;

	memcpy(glw_state->drawArray, tess.xyz, sizeof(vec4_t) * tess.numVertexes);
	glw_state->drawArray += tess.numVertexes * 4;

	memcpy(glw_state->drawArray, tess.normal, sizeof(vec4_t) * tess.numVertexes);
	glw_state->drawArray += tess.numVertexes * 4; 

	memcpy(glw_state->drawArray, tess.svars.colors, sizeof(D3DCOLOR) * tess.numVertexes);
	glw_state->drawArray += tess.numVertexes;
 
	// Write the vertex shader
#define CMD_STREAM_STRIDEANDTYPE0 0x1760
	*glw_state->drawArray++ = D3DPUSH_ENCODE(CMD_STREAM_STRIDEANDTYPE0, 16);

	// Position
	*glw_state->drawArray++ = (16 << 8)|D3DVSDT_FLOAT3;

	// Normal
	*glw_state->drawArray++ = (16 << 8) | D3DVSDT_FLOAT3;

	// Color
	*glw_state->drawArray++ = (4 << 8) | D3DVSDT_D3DCOLOR;

	for(int i = 0; i < 13; i++)
	{
		*glw_state->drawArray++ = ((glw_state->drawStride * 4) << 8) | D3DVSDT_NONE;
	}

	//	 Write the indicator to our vertex stream
	*glw_state->drawArray++ = D3DPUSH_ENCODE(0x1720, 1);
	*glw_state->drawArray++ = (DWORD)stream & 0x7fffffff;
	tess.pXyz = stream;
	stream += tess.numVertexes * 4;

	*glw_state->drawArray++ = D3DPUSH_ENCODE(0x1724, 1);
	*glw_state->drawArray++ = (DWORD)stream & 0x7fffffff;
	tess.pNormal = stream;
	stream += tess.numVertexes * 4;

	*glw_state->drawArray++ = D3DPUSH_ENCODE(0x1728, 1);
	*glw_state->drawArray++ = (DWORD)stream & 0x7fffffff;
	stream += tess.numVertexes;

	// Send thru the index data
	PushIndices(tess.numIndexes, (GLushort*)tess.indexes);

	// finish up the draw
	glw_state->inDrawBlock = false;

	DWORD* push = _terminateIndexPacket(glw_state->drawArray);

	glw_state->device->EndPush(push);
}
#endif

// EXTENSION: Take an array of triangle indices and draw
// the appropriate triangle strips.  Virtually ALL geometry
// is drawn with this function so it better be fast.
static void dllIndexedTriToStrip(GLsizei count, const GLushort *indices)
{
//#ifndef _XBOX
#if GLW_USE_TRI_STRIPS

	// update the render state
	_updateDrawStride(glw_state->normalArrayState,
		glw_state->texCoordArrayState[0] ? count : 0,
		glw_state->texCoordArrayState[1] ? count : 0);
	_updateShader(glw_state->normalArrayState,
		glw_state->texCoordArrayState[0],
		glw_state->texCoordArrayState[1]);
	_updateTextures();
	_updateMatrices();
	
	// convert triangles to strips -- guarantees that
	// no strip exceeds the max draw packet size
	if(tess.currentPass == 0)
	{
		buildStrips(glw_state->strip_lengths, 
			&glw_state->num_strip_lengths, glw_state->strip_dest, &count, indices);
	}

	// Yeah, its a hack, but I gotta do this so bumpmapping
	// doesnt go all crazy on the 'force speed' effect and 
	// 'disintegration' effect
	if(tess.shader && 
		tess.shader->isBumpMap && 
		(backEnd.currentEntity->e.renderfx & 
// VVFIXME : This is probably wrong. It looks like RF_ALPHA_FADE is renamed
// RF_RGB_TINT in MP. Substitute?
#ifndef _JK2MP
		(RF_ALPHA_FADE | RF_DISINTEGRATE1 | RF_DISINTEGRATE2)))
#else
		(RF_DISINTEGRATE1 | RF_DISINTEGRATE2)))
#endif
	{
		if(tess.currentPass != 2)
			return;
	}

#ifdef _XBOX
	glw_state->primitiveMode = D3DPT_TRIANGLESTRIP;

	// get the necessary draw function
	drawelemfunc_t func = _drawElementFuncTable[_getDrawFunc()];
	int stride = glw_state->drawStride;
	
	int index = 0;
	for (int l = 0; l < glw_state->num_strip_lengths; ++l)
	{
		int cur_len = glw_state->strip_lengths[l];
		
		// start a draw packet
		DWORD* push;
		glw_state->device->BeginPush(stride * cur_len + 5, &push);
		push = _restartDrawPacket(push, cur_len);
		
		// draw the geometry
		glw_state->drawArray = push;
		func(cur_len, &glw_state->strip_dest[index]);
		index += cur_len;
		
		// finish the draw packet
		push = _terminateDrawPacket(&push[stride * cur_len]);
		glw_state->device->EndPush(push);
	}
#else _XBOX
	// simplified render on the PC
	int index = 0;
	for (int l = 0; l < glw_state->num_strip_lengths; ++l)
	{
		dllDrawElements(GL_TRIANGLE_STRIP, glw_state->strip_lengths[l], 
			GL_UNSIGNED_SHORT, &glw_state->strip_dest[index]);
		index += glw_state->strip_lengths[l];
	}
#endif _XBOX

#else GLW_USE_TRI_STRIPS
	// just render simple triangles
	dllDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_SHORT, indices);
#endif GLW_USE_TRI_STRIPS
//#endif 
}

static void dllIndexMask(GLuint mask)
{
	assert(0);
}

static void dllIndexPointer(GLenum type, GLsizei stride, const GLvoid *pointer)
{
	assert(0);
}

static void dllIndexd(GLdouble c)
{
	assert(0);
}

static void dllIndexdv(const GLdouble *c)
{
	assert(0);
}

static void dllIndexf(GLfloat c)
{
	assert(0);
}

static void dllIndexfv(const GLfloat *c)
{
	assert(0);
}

static void dllIndexi(GLint c)
{
	assert(0);
}

static void dllIndexiv(const GLint *c)
{
	assert(0);
}

static void dllIndexs(GLshort c)
{
	assert(0);
}

static void dllIndexsv(const GLshort *c)
{
	assert(0);
}

static void dllIndexub(GLubyte c)
{
	assert(0);
}

static void dllIndexubv(const GLubyte *c)
{
	assert(0);
}

static void dllInitNames(void)
{
	assert(0);
}

static void dllInterleavedArrays(GLenum format, GLsizei stride, const GLvoid *pointer)
{
	assert(0);
}

GLboolean dllIsEnabled(GLenum cap)
{
	DWORD flag;
	switch (cap)
	{
	case GL_ALPHA_TEST: glw_state->device->GetRenderState(D3DRS_ALPHATESTENABLE, &flag); break;
	case GL_BLEND: glw_state->device->GetRenderState(D3DRS_ALPHABLENDENABLE, &flag); break;
	case GL_CULL_FACE: return glw_state->cullEnable;
	case GL_DEPTH_TEST: glw_state->device->GetRenderState(D3DRS_ZENABLE, &flag); break;
	case GL_FOG: glw_state->device->GetRenderState(D3DRS_FOGENABLE, &flag); break;
	case GL_LIGHTING: glw_state->device->GetRenderState(D3DRS_LIGHTING, &flag); break;
#ifdef _XBOX
	case GL_POLYGON_OFFSET_FILL: glw_state->device->GetRenderState(D3DRS_SOLIDOFFSETENABLE, &flag); break;
#else
	case GL_POLYGON_OFFSET_FILL: return FALSE;
#endif
	case GL_SCISSOR_TEST: return glw_state->scissorEnable;
	case GL_STENCIL_TEST: glw_state->device->GetRenderState(D3DRS_STENCILENABLE, &flag); break;
	case GL_TEXTURE_2D: return glw_state->textureStageEnable[glw_state->serverTU];
	default: return FALSE;
	}
	return flag;
}

GLboolean dllIsList(GLuint lnum)
{
	assert(0);
	return 1;
}

GLboolean dllIsTexture(GLuint texture)
{
	assert(0);
	return 1;
}

static void dllLightModelf(GLenum pname, GLfloat param)
{
	assert(0);
}

static void dllLightModelfv(GLenum pname, const GLfloat *params)
{
	assert(0);
}

static void dllLightModeli(GLenum pname, GLint param)
{
	assert(0);
}

static void dllLightModeliv(GLenum pname, const GLint *params)
{
	assert(0);
}

static void dllLightf(GLenum light, GLenum pname, GLfloat param)
{
	assert(0);
}

static void dllLightfv(GLenum light, GLenum pname, const GLfloat *params)
{
	switch(pname)
	{
	case GL_AMBIENT:
		{
			glw_state->dirLight.Ambient.r = params[0] / 255.0f;
			glw_state->dirLight.Ambient.g = params[1] / 255.0f;
			glw_state->dirLight.Ambient.b = params[2] / 255.0f;
		}
		break;

	case GL_DIFFUSE:
		{
			glw_state->dirLight.Diffuse.r = params[0] / 255.0f;
			glw_state->dirLight.Diffuse.g = params[1] / 255.0f;
			glw_state->dirLight.Diffuse.b = params[2] / 255.0f;
		}
		break;

	case GL_SPECULAR:
		{
			glw_state->dirLight.Specular.r = params[0] / 255.0f;
			glw_state->dirLight.Specular.g = params[1] / 255.0f;
			glw_state->dirLight.Specular.b = params[2] / 255.0f;
		}
		break;
	case GL_POSITION:
		{
			glw_state->dirLight.Position.x = params[0];
			glw_state->dirLight.Position.y = params[1];
			glw_state->dirLight.Position.z = params[2];
		}
		break;

	case GL_SPOT_DIRECTION:
		{
			glw_state->dirLight.Direction.x = -params[0];
			glw_state->dirLight.Direction.y = -params[1];
			glw_state->dirLight.Direction.z = -params[2];
		}
		break;

	default:
		assert(0);
		break;
	}

	glw_state->device->SetLight(light, &glw_state->dirLight);
	glw_state->device->LightEnable(light, TRUE);
}

static void dllLighti(GLenum light, GLenum pname, GLint param)
{
	assert(0);
}

static void dllLightiv(GLenum light, GLenum pname, const GLint *params)
{
	assert(0);
}

static void dllLineStipple(GLint factor, GLushort pattern)
{
	assert(0);
}

static void dllLineWidth(GLfloat width)
{
//	assert(0);
}

static void dllListBase(GLuint base)
{
	assert(0);
}

static void dllLoadIdentity(void)
{
	glw_state->matrixStack[glw_state->matrixMode]->LoadIdentity();
	glw_state->matricesDirty[glw_state->matrixMode] = true;
}

static void dllLoadMatrixd(const GLdouble *m)
{
	assert(0);
}

static void dllLoadMatrixf(const GLfloat *m)
{
	glw_state->matrixStack[glw_state->matrixMode]->LoadMatrix((D3DXMATRIX*)m);
	glw_state->matricesDirty[glw_state->matrixMode] = true;
}

static void dllLoadName(GLuint name)
{
	assert(0);
}

static void dllLogicOp(GLenum opcode)
{
	assert(0);
}

static void dllMap1d(GLenum target, GLdouble u1, GLdouble u2, GLint stride, GLint order, const GLdouble *points)
{
	assert(0);
}

static void dllMap1f(GLenum target, GLfloat u1, GLfloat u2, GLint stride, GLint order, const GLfloat *points)
{
	assert(0);
}

static void dllMap2d(GLenum target, GLdouble u1, GLdouble u2, GLint ustride, GLint uorder, GLdouble v1, GLdouble v2, GLint vstride, GLint vorder, const GLdouble *points)
{
	assert(0);
}

static void dllMap2f(GLenum target, GLfloat u1, GLfloat u2, GLint ustride, GLint uorder, GLfloat v1, GLfloat v2, GLint vstride, GLint vorder, const GLfloat *points)
{
	assert(0);
}

static void dllMapGrid1d(GLint un, GLdouble u1, GLdouble u2)
{
	assert(0);
}

static void dllMapGrid1f(GLint un, GLfloat u1, GLfloat u2)
{
	assert(0);
}

static void dllMapGrid2d(GLint un, GLdouble u1, GLdouble u2, GLint vn, GLdouble v1, GLdouble v2)
{
	assert(0);
}

static void dllMapGrid2f(GLint un, GLfloat u1, GLfloat u2, GLint vn, GLfloat v1, GLfloat v2)
{
	assert(0);
}

static void dllMaterialf(GLenum face, GLenum pname, GLfloat param)
{
	assert(0);
}

static void dllMaterialfv(GLenum face, GLenum pname, const GLfloat *params)
{
	switch(pname)
	{
	case GL_AMBIENT:
		glw_state->mtrl.Ambient.r = params[0] / 255.0f;
		glw_state->mtrl.Ambient.g = params[1] / 255.0f;
		glw_state->mtrl.Ambient.b = params[2] / 255.0f;
		glw_state->mtrl.Ambient.a = params[3] / 255.0f;
		break;

	case GL_DIFFUSE:
		glw_state->mtrl.Diffuse.r = params[0] / 255.0f;
		glw_state->mtrl.Diffuse.g = params[1] / 255.0f;
		glw_state->mtrl.Diffuse.b = params[2] / 255.0f;
		glw_state->mtrl.Diffuse.a = params[3] / 255.0f;
		break;

	case GL_SPECULAR:
		glw_state->mtrl.Specular.r = params[0] / 255.0f;
		glw_state->mtrl.Specular.g = params[1] / 255.0f;
		glw_state->mtrl.Specular.b = params[2] / 255.0f;
		glw_state->mtrl.Specular.a = params[3] / 255.0f;
		break;

	case GL_EMISSION:
		glw_state->mtrl.Emissive.r = params[0] / 255.0f;
		glw_state->mtrl.Emissive.g = params[1] / 255.0f;
		glw_state->mtrl.Emissive.b = params[2] / 255.0f;
		glw_state->mtrl.Emissive.a = params[3] / 255.0f;
		break;

	default:
		assert(0);
		break;
	}

	if (glw_state->device) glw_state->device->SetMaterial(&glw_state->mtrl);
}

static void dllMateriali(GLenum face, GLenum pname, GLint param)
{
	assert(0);
}

static void dllMaterialiv(GLenum face, GLenum pname, const GLint *params)
{
	assert(0);
}

static void dllMatrixMode(GLenum mode)
{
	switch (mode)
	{
	case GL_MODELVIEW: glw_state->matrixMode = glwstate_t::MatrixMode_Model; break;
	case GL_PROJECTION: glw_state->matrixMode = glwstate_t::MatrixMode_Projection; break;
#ifdef _XBOX
	case GL_TEXTURE0: glw_state->matrixMode = glwstate_t::MatrixMode_Texture0; break;
	case GL_TEXTURE1: glw_state->matrixMode = glwstate_t::MatrixMode_Texture1; break;
#endif
	default: assert(false); break;
	}
}

static void dllMultMatrixd(const GLdouble *m)
{
	assert(0);
}

static void dllMultMatrixf(const GLfloat *m)
{
	glw_state->matrixStack[glw_state->matrixMode]->MultMatrixLocal((D3DXMATRIX*)m);
	glw_state->matricesDirty[glw_state->matrixMode] = true;
}

static void dllNewList(GLuint lnum, GLenum mode)
{
	assert(0);
}

static void setNormal(float x, float y, float z)
{
	assert(glw_state->inDrawBlock);
	s_immediateNormal[0] = x;
	s_immediateNormal[1] = y;
	s_immediateNormal[2] = z;
}
static void dllNormal3b(GLbyte nx, GLbyte ny, GLbyte nz)
{
	assert(0);
}

static void dllNormal3bv(const GLbyte *v)
{
	assert(0);
}

static void dllNormal3d(GLdouble nx, GLdouble ny, GLdouble nz)
{
	assert(0);
}

static void dllNormal3dv(const GLdouble *v)
{
	assert(0);
}

static void dllNormal3f(GLfloat nx, GLfloat ny, GLfloat nz)
{
	setNormal(nx, ny, nz);
}

static void dllNormal3fv(const GLfloat *v)
{
	setNormal(v[0], v[1], v[2]);
}

static void dllNormal3i(GLint nx, GLint ny, GLint nz)
{
	assert(0);
}

static void dllNormal3iv(const GLint *v)
{
	assert(0);
}

static void dllNormal3s(GLshort nx, GLshort ny, GLshort nz)
{
	assert(0);
}

static void dllNormal3sv(const GLshort *v)
{
	assert(0);
}

static void dllNormalPointer(GLenum type, GLsizei stride, const GLvoid *pointer)
{
	assert(type == GL_FLOAT);
	
	stride = (stride == 0) ? (sizeof(GLfloat) * 3) : stride;

	glw_state->normalPointer = pointer;
	glw_state->normalStride = stride;
}

static void dllOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar)
{
	D3DXMATRIX m;
	D3DXMatrixOrthoOffCenterRH(&m, left, right, top, bottom, zNear, zFar);
	glw_state->matrixStack[glw_state->matrixMode]->MultMatrix(&m);
	glw_state->matricesDirty[glw_state->matrixMode] = true;
}

static void dllPassThrough(GLfloat token)
{
	assert(0);
}

static void dllPixelMapfv(GLenum map, GLsizei mapsize, const GLfloat *values)
{
	assert(0);
}

static void dllPixelMapuiv(GLenum map, GLsizei mapsize, const GLuint *values)
{
	assert(0);
}

static void dllPixelMapusv(GLenum map, GLsizei mapsize, const GLushort *values)
{
	assert(0);
}

static void dllPixelStoref(GLenum pname, GLfloat param)
{
	assert(0);
}

static void dllPixelStorei(GLenum pname, GLint param)
{
	assert(0);
}

static void dllPixelTransferf(GLenum pname, GLfloat param)
{
	assert(0);
}

static void dllPixelTransferi(GLenum pname, GLint param)
{
	assert(0);
}

static void dllPixelZoom(GLfloat xfactor, GLfloat yfactor)
{
	assert(0);
}

static void dllPointSize(GLfloat size)
{
	glw_state->device->SetRenderState(D3DRS_POINTSCALEENABLE, TRUE);
	glw_state->device->SetRenderState(D3DRS_POINTSIZE, *((DWORD*)&size));
}

static void dllPolygonMode(GLenum face, GLenum mode)
{
	D3DFILLMODE m;
	switch (mode)
	{
	case GL_POINT: m = D3DFILL_POINT; break;
	case GL_LINE: m = D3DFILL_WIREFRAME; break;
	case GL_FILL: m = D3DFILL_SOLID; break;
	default: assert(0); break;
	}

	switch (face)
	{
	case GL_FRONT:
		glw_state->device->SetRenderState(D3DRS_FILLMODE, m);
		break;
	case GL_BACK:
#ifdef _XBOX
		glw_state->device->SetRenderState(D3DRS_BACKFILLMODE, (DWORD)m);
#endif
		break;
	case GL_FRONT_AND_BACK:
		glw_state->device->SetRenderState(D3DRS_FILLMODE, m);
#ifdef _XBOX
		glw_state->device->SetRenderState(D3DRS_BACKFILLMODE, (DWORD)m);
#endif
		break;
	}
}

static void dllPolygonOffset(GLfloat factor, GLfloat units)
{
#ifdef _XBOX
	glw_state->device->SetRenderState(D3DRS_POLYGONOFFSETZOFFSET, *((DWORD*)&factor));
	glw_state->device->SetRenderState(D3DRS_POLYGONOFFSETZSLOPESCALE, *((DWORD*)&units));
#endif
}

static void dllPolygonStipple(const GLubyte *mask)
{
	assert(0);
}

static void dllPopAttrib(void)
{
	assert(0);
}

static void dllPopClientAttrib(void)
{
	assert(0);
}

static void dllPopMatrix(void)
{
	glw_state->matrixStack[glw_state->matrixMode]->Pop();
	glw_state->matricesDirty[glw_state->matrixMode] = true;
}

static void dllPopName(void)
{
	assert(0);
}

static void dllPrioritizeTextures(GLsizei n, const GLuint *textures, const GLclampf *priorities)
{
	assert(0);
}

static void dllPushAttrib(GLbitfield mask)
{
	assert(0);
}

static void dllPushClientAttrib(GLbitfield mask)
{
	assert(0);
}

static void dllPushMatrix(void)
{
	glw_state->matrixStack[glw_state->matrixMode]->Push();
	glw_state->matricesDirty[glw_state->matrixMode] = true;
}

static void dllPushName(GLuint name)
{
	assert(0);
}

static void dllRasterPos2d(GLdouble x, GLdouble y)
{
	assert(0);
}

static void dllRasterPos2dv(const GLdouble *v)
{
	assert(0);
}

static void dllRasterPos2f(GLfloat x, GLfloat y)
{
	assert(0);
}

static void dllRasterPos2fv(const GLfloat *v)
{
	assert(0);
}

static void dllRasterPos2i(GLint x, GLint y)
{
	assert(0);
}

static void dllRasterPos2iv(const GLint *v)
{
	assert(0);
}

static void dllRasterPos2s(GLshort x, GLshort y)
{
	assert(0);
}

static void dllRasterPos2sv(const GLshort *v)
{
	assert(0);
}

static void dllRasterPos3d(GLdouble x, GLdouble y, GLdouble z)
{
	assert(0);
}

static void dllRasterPos3dv(const GLdouble *v)
{
	assert(0);
}

static void dllRasterPos3f(GLfloat x, GLfloat y, GLfloat z)
{
	assert(0);
}

static void dllRasterPos3fv(const GLfloat *v)
{
	assert(0);
}

static void dllRasterPos3i(GLint x, GLint y, GLint z)
{
	assert(0);
}

static void dllRasterPos3iv(const GLint *v)
{
	assert(0);
}

static void dllRasterPos3s(GLshort x, GLshort y, GLshort z)
{
	assert(0);
}

static void dllRasterPos3sv(const GLshort *v)
{
	assert(0);
}

static void dllRasterPos4d(GLdouble x, GLdouble y, GLdouble z, GLdouble w)
{
	assert(0);
}

static void dllRasterPos4dv(const GLdouble *v)
{
	assert(0);
}

static void dllRasterPos4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
	assert(0);
}

static void dllRasterPos4fv(const GLfloat *v)
{
	assert(0);
}

static void dllRasterPos4i(GLint x, GLint y, GLint z, GLint w)
{
	assert(0);
}

static void dllRasterPos4iv(const GLint *v)
{
	assert(0);
}

static void dllRasterPos4s(GLshort x, GLshort y, GLshort z, GLshort w)
{
	assert(0);
}

static void dllRasterPos4sv(const GLshort *v)
{
	assert(0);
}

static void dllReadBuffer(GLenum mode)
{
	assert(0);
}

//static void dllReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLsizei twidth, GLsizei theight, GLvoid *pixels)
static void dllReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels)
{
	assert( 0 );
	return;

}

/**********
dllCopyBackBufferToTex
Does a direct copy of the backbuffer to the current texture. The current texture
must be linear, and it must be 640 x 480 in size. If a more complex copy is
needed, use dllCopyBackBufferToTexEXT.
**********/
static void dllCopyBackBufferToTex()
{
	glwstate_t::TextureInfo* info = _getCurrentTexture(glw_state->serverTU);
	if (info == NULL) return;

	LPDIRECT3DSURFACE8 surf;
	LPDIRECT3DSURFACE8 backbuffer;

	info->mipmap->GetSurfaceLevel(0, &surf);
	glw_state->device->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &backbuffer);

	glw_state->device->CopyRects(backbuffer, NULL, 0, surf, NULL);

	surf->Release();
	backbuffer->Release();
}

/**********
dllCopyBackBufferToTexEXT
Copies a portion of the backbuffer to a texture
If the destination is a DXT1 texture, then the buffer will be compressed
width	- width of the backbuffer polygon rendered to the destination texture
height	- height of the backbuffer polygon rendered to the destination texture
u,v		- describes the potion of the backbuffer to be copied in screen coords

The active texture (that we're replacing) NEEDS to already have enough space!
**********/
static void dllCopyBackBufferToTexEXT(float width, float height, float u1, float v1, float u2, float v2)
{
	glwstate_t::TextureInfo* info = _getCurrentTexture(glw_state->serverTU);
	if (info == NULL || info->mipmap == NULL || info->data == NULL ||
		glw_state == NULL || glw_state->device == NULL)
	{
		return;
	}

	const UINT targetWidth = (UINT)(width + 0.5f);
	const UINT targetHeight = (UINT)(height + 0.5f);
	if (targetWidth == 0 || targetHeight == 0)
	{
		return;
	}

	struct QUAD { D3DXVECTOR4 p; FLOAT tu,tv;} q[4];
	q[0].p	= D3DXVECTOR4( 0.0f, 0.0f, 1.0f, 1.0f );
	q[0].tu = u1;		q[0].tv = v1;
	q[1].p	= D3DXVECTOR4( width, 0.0f, 1.0f, 1.0f );
	q[1].tu = u2;	q[1].tv = v1;
	q[2].p	= D3DXVECTOR4( 0.0f, height , 1.0f, 1.0f );
	q[2].tu = u1;		q[2].tv = v2;
	q[3].p	= D3DXVECTOR4( width, height, 1.0f, 1.0f );
	q[3].tu = u2;	q[3].tv = v2;


	LPDIRECT3DSURFACE8	pSurface = NULL;
	LPDIRECT3DSURFACE8	pRenderTarget = NULL;
	LPDIRECT3DSURFACE8	pStencilBuffer = NULL;
	D3DSURFACE_DESC		desc;
	D3DTexture*			pRenderTex = NULL;
	D3DVIEWPORT8		savedViewport;
	D3DVIEWPORT8		targetViewport;

	DWORD srcblend, destblend, alphablend, alphatest, zwrite, zenable, vShader, pShader;
	DWORD colorop, colorarg1, addressu, addressv, minfilter, magfilter, colorwriteenable;

#ifdef _XBOX
	g_SPXBPhaseLast = 0x43425230; /* 'CBR0' */
#endif

	// save the current state
	glw_state->device->GetRenderState( D3DRS_SRCBLEND, &srcblend );
	glw_state->device->GetRenderState( D3DRS_DESTBLEND, &destblend );
	glw_state->device->GetRenderState( D3DRS_ALPHABLENDENABLE, &alphablend );
	glw_state->device->GetRenderState( D3DRS_ALPHATESTENABLE, &alphatest );
	glw_state->device->GetRenderState( D3DRS_ZWRITEENABLE, &zwrite );
	glw_state->device->GetRenderState( D3DRS_ZENABLE, &zenable );
	glw_state->device->GetRenderState( D3DRS_COLORWRITEENABLE, &colorwriteenable);
	glw_state->device->GetVertexShader( &vShader );
	glw_state->device->GetPixelShader( &pShader );
	glw_state->device->GetTextureStageState(0, D3DTSS_COLOROP, &colorop);
	glw_state->device->GetTextureStageState(0, D3DTSS_COLORARG1, &colorarg1);
	glw_state->device->GetTextureStageState(0, D3DTSS_ADDRESSU, &addressu);
	glw_state->device->GetTextureStageState(0, D3DTSS_ADDRESSV, &addressv);
	glw_state->device->GetTextureStageState(0, D3DTSS_MINFILTER, &minfilter);
	glw_state->device->GetTextureStageState(0, D3DTSS_MAGFILTER, &magfilter);

	// Save the actual active target. This matters for split-screen and any
	// caller that is already rendering offscreen.
	glw_state->device->GetRenderTarget(&pRenderTarget);
	glw_state->device->GetDepthStencilSurface(&pStencilBuffer);
	glw_state->device->GetViewport(&savedViewport);
	if (pRenderTarget == NULL)
	{
#ifdef _XBOX
		g_SPXBPhaseLast = 0x43425246; /* 'CBRF' */
		XBL("Native D3D8 back-buffer copy has no active render target\n");
#endif
		return;
	}

	// get a surface desc
	info->mipmap->GetLevelDesc(0, &desc);

	// Check to see if the texture needs to be resized
	if( desc.Width != targetWidth || desc.Height != targetHeight)
	{
		IDirect3DTexture8 resizedHeader;
		const DWORD requiredSize = XGSetTextureHeader(
			targetWidth,
			targetHeight,
			1,
			0,
			desc.Format,
			(D3DPOOL)0,
			&resizedHeader,
			0,
			0);

		info->mipmap->BlockUntilNotBusy();

		if (requiredSize > info->dataSize)
		{
			if (info->data)
			{
				if (!gTextures.Free(info->data, info->dataSize))
				{
					g_SPXBPhaseLast = 0x43424652; /* 'CBFR' */
					Com_Error(ERR_FATAL,
						"Native D3D8 back-buffer texture resize could not release old allocation");
					return;
				}
				info->data = NULL;
				info->dataSize = 0;
			}
			void *resizedData = gTextures.Allocate(requiredSize,
				glw_state->currentTexture[glw_state->serverTU]);
			if (resizedData == NULL)
			{
#ifdef _XBOX
				g_SPXBPhaseLast = 0x4342414C; /* 'CBAL' */
				XBLF("Native D3D8 back-buffer texture resize failed: request=%u available=%u largest=%u\n",
					(unsigned)requiredSize, (unsigned)gTextures.Free(),
					(unsigned)gTextures.LargestFree());
#endif
				pRenderTarget->Release();
				if (pStencilBuffer)
				{
					pStencilBuffer->Release();
				}
				return;
			}
			info->data = resizedData;
			info->dataSize = requiredSize;
		}

		XGSetTextureHeader(targetWidth,
						  targetHeight,
						  1,
						  0,
						  desc.Format,
						  (D3DPOOL)0,
						  info->mipmap,
						  0,
						  0);

		info->mipmap->Register( info->data );
	}

	// Xbox render targets are linear surfaces. Always render into a linear
	// temporary, then convert into the caller's sampling format after restoring
	// the live target. Binding a swizzled texture here faults on retail D3D8.
	HRESULT createResult = glw_state->device->CreateTexture(
		targetWidth,
		targetHeight,
		1,
		0,
		D3DFMT_LIN_X8R8G8B8,
		(D3DPOOL)0,
		&pRenderTex);
	if (FAILED(createResult) || pRenderTex == NULL)
	{
#ifdef _XBOX
		g_SPXBPhaseLast = 0x43425446; /* 'CBTF' */
		XBLF("Native D3D8 back-buffer render texture failed: hr=0x%08x size=%ux%u\n",
			(unsigned)createResult, (unsigned)targetWidth,
			(unsigned)targetHeight);
#endif
		pRenderTarget->Release();
		if (pStencilBuffer)
		{
			pStencilBuffer->Release();
		}
		return;
	}

	// make our current surface a render target
	pRenderTex->GetSurfaceLevel(0, &pSurface);
	if (pSurface == NULL)
	{
		pRenderTex->Release();
		pRenderTarget->Release();
		if (pStencilBuffer)
		{
			pStencilBuffer->Release();
		}
		return;
	}

#ifdef _XBOX
	g_SPXBPhaseLast = 0x43425231; /* 'CBR1' */
#endif
	// This function intentionally invalidates texture bindings. The qgl
	// cache will restore every active stage on the next draw.
	for (int unbindStage = 0; unbindStage < GLW_MAX_TEXTURE_STAGES; ++unbindStage)
	{
		glw_state->device->SetTexture(unbindStage, NULL);
	}
	glw_state->device->SetRenderTarget( pSurface, NULL );

	targetViewport.X = 0;
	targetViewport.Y = 0;
	targetViewport.Width = targetWidth;
	targetViewport.Height = targetHeight;
	targetViewport.MinZ = 0.0f;
	targetViewport.MaxZ = 1.0f;
	glw_state->device->SetViewport(&targetViewport);
	
	// set texture 0 to the saved render-target data
	glw_state->device->SetTexture(0,(LPDIRECT3DTEXTURE8)pRenderTarget);

	// set the texture 0 state
	glw_state->device->SetTextureStageState( 0, D3DTSS_COLOROP,   D3DTOP_SELECTARG1 );
    glw_state->device->SetTextureStageState( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
    glw_state->device->SetTextureStageState( 0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP );
    glw_state->device->SetTextureStageState( 0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP );
    glw_state->device->SetTextureStageState( 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR );
    glw_state->device->SetTextureStageState( 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR );
	
	// set the render state
	glw_state->device->SetRenderState( D3DRS_ZENABLE,         FALSE );
    glw_state->device->SetRenderState( D3DRS_ALPHATESTENABLE, FALSE );
	glw_state->device->SetRenderState( D3DRS_ALPHABLENDENABLE, FALSE);
	glw_state->device->SetRenderState( D3DRS_SRCBLEND, D3DBLEND_SRCALPHA );
	glw_state->device->SetRenderState( D3DRS_DESTBLEND, D3DBLEND_SRCALPHA | D3DBLEND_INVSRCALPHA );
	glw_state->device->SetRenderState( D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_ALL);

	// set our vertex shader and draw the backbuffer to the texture
	glw_state->device->SetVertexShader( D3DFVF_XYZRHW|D3DFVF_TEX1 );
	glw_state->device->SetPixelShader( NULL );
	glw_state->device->Clear(NULL,NULL,D3DCLEAR_TARGET,D3DCOLOR_COLORVALUE(1.0f, 1.0f, 1.0f, 1.0f), 1.0f, 0);
	glw_state->device->DrawPrimitiveUP( D3DPT_QUADSTRIP, 1, q, sizeof(QUAD) );

#ifdef _XBOX
	g_SPXBPhaseLast = 0x43425232; /* 'CBR2' */
#endif

	// A surface cannot remain bound as a texture while becoming the render
	// target again. Restore the target and viewport before locking or
	// compressing the temporary texture.
	glw_state->device->SetTexture(0, NULL);
	glw_state->device->SetRenderTarget(pRenderTarget, pStencilBuffer);
	glw_state->device->SetViewport(&savedViewport);

#ifdef _XBOX
	g_SPXBPhaseLast = 0x43425233; /* 'CBR3' */
#endif

	// Convert the linear render result into the destination's sampling layout.
	{
		D3DLOCKED_RECT		srcLock;
		D3DLOCKED_RECT		dstLock;
		const HRESULT srcLockResult = pRenderTex->LockRect(0, &srcLock, NULL, 0);
		const HRESULT dstLockResult = info->mipmap->LockRect(0, &dstLock, NULL, 0);

		if (SUCCEEDED(srcLockResult) && SUCCEEDED(dstLockResult) &&
			srcLock.pBits != NULL && dstLock.pBits != NULL)
		{
			if (desc.Format == D3DFMT_DXT1 || desc.Format == D3DFMT_DXT5)
			{
				XGCompressRect(dstLock.pBits,
					desc.Format,
					dstLock.Pitch,
					targetWidth,
					targetHeight,
					srcLock.pBits,
					D3DFMT_LIN_X8R8G8B8,
					srcLock.Pitch,
					1,
					0);
			}
			else if (desc.Format == D3DFMT_A8R8G8B8 ||
				desc.Format == D3DFMT_X8R8G8B8)
			{
				XGSwizzleRect(srcLock.pBits,
					srcLock.Pitch,
					NULL,
					dstLock.pBits,
					targetWidth,
					targetHeight,
					NULL,
					4);
			}
			else if (desc.Format == D3DFMT_R5G6B5 ||
				desc.Format == D3DFMT_A4R4G4B4)
			{
				const DWORD linearBytes = targetWidth * targetHeight * sizeof(WORD);
				WORD *linear16 = (WORD*)Z_Malloc(linearBytes,
					TAG_TEMP_WORKSPACE, qfalse, 4);
				if (linear16 != NULL)
				{
					for (UINT y = 0; y < targetHeight; ++y)
					{
						const DWORD *srcRow = (const DWORD*)
							((const byte*)srcLock.pBits + y * srcLock.Pitch);
						WORD *dstRow = linear16 + y * targetWidth;
						for (UINT x = 0; x < targetWidth; ++x)
						{
							const DWORD pixel = srcRow[x];
							const DWORD b = pixel & 0xff;
							const DWORD g = (pixel >> 8) & 0xff;
							const DWORD r = (pixel >> 16) & 0xff;
							if (desc.Format == D3DFMT_R5G6B5)
							{
								dstRow[x] = (WORD)(((r >> 3) << 11) |
									((g >> 2) << 5) | (b >> 3));
							}
							else
							{
								const DWORD a = (pixel >> 24) & 0xff;
								dstRow[x] = (WORD)(((a >> 4) << 12) |
									((r >> 4) << 8) | ((g >> 4) << 4) |
									(b >> 4));
							}
						}
					}
					XGSwizzleRect(linear16,
						targetWidth * sizeof(WORD),
						NULL,
						dstLock.pBits,
						targetWidth,
						targetHeight,
						NULL,
						sizeof(WORD));
					Z_Free(linear16);
				}
			}
			else if (desc.Format == D3DFMT_LIN_A8R8G8B8 ||
				desc.Format == D3DFMT_LIN_X8R8G8B8)
			{
				const DWORD rowBytes = targetWidth * sizeof(DWORD);
				for (UINT y = 0; y < targetHeight; ++y)
				{
					memcpy((byte*)dstLock.pBits + y * dstLock.Pitch,
						(const byte*)srcLock.pBits + y * srcLock.Pitch,
						rowBytes);
				}
			}
			else
			{
#ifdef _XBOX
				g_SPXBPhaseLast = 0x4342464D; /* 'CBFM' */
				XBLF("Native D3D8 back-buffer destination format unsupported: 0x%08x\n",
					(unsigned)desc.Format);
#endif
			}
		}
		else
		{
#ifdef _XBOX
			g_SPXBPhaseLast = 0x43424C46; /* 'CBLF' */
			XBLF("Native D3D8 back-buffer texture lock failed: src=0x%08x dst=0x%08x\n",
				(unsigned)srcLockResult, (unsigned)dstLockResult);
#endif
		}

		if (SUCCEEDED(srcLockResult))
		{
			pRenderTex->UnlockRect(0);
		}
		if (SUCCEEDED(dstLockResult))
		{
			info->mipmap->UnlockRect(0);
		}
	}
	pRenderTex->Release();

#ifdef _XBOX
	g_SPXBPhaseLast = 0x43425234; /* 'CBR4' */
#endif

	// return our state
	glw_state->device->SetRenderState( D3DRS_SRCBLEND, srcblend );
	glw_state->device->SetRenderState( D3DRS_DESTBLEND, destblend );
	glw_state->device->SetRenderState( D3DRS_ALPHABLENDENABLE, alphablend );
	glw_state->device->SetRenderState( D3DRS_ALPHATESTENABLE, alphatest );
	glw_state->device->SetRenderState( D3DRS_ZWRITEENABLE, zwrite );
	glw_state->device->SetRenderState( D3DRS_ZENABLE, zenable );
	glw_state->device->SetRenderState( D3DRS_COLORWRITEENABLE, colorwriteenable);

	// Clear stage zero again. We're not being nice.
	glw_state->device->SetTexture(0, NULL);
	for (int stage = 0; stage < GLW_MAX_TEXTURE_STAGES; ++stage)
	{
		glw_state->textureStageDirty[stage] = true;
	}

	glw_state->device->SetTextureStageState(0, D3DTSS_COLOROP, colorop);
	glw_state->device->SetTextureStageState(0, D3DTSS_COLORARG1, colorarg1);
	glw_state->device->SetTextureStageState(0, D3DTSS_ADDRESSU, addressu);
	glw_state->device->SetTextureStageState(0, D3DTSS_ADDRESSV, addressv);
	glw_state->device->SetTextureStageState(0, D3DTSS_MINFILTER, minfilter);
	glw_state->device->SetTextureStageState(0, D3DTSS_MAGFILTER, magfilter);

	glw_state->device->SetVertexShader( vShader );
	glw_state->device->SetPixelShader( pShader );

	pSurface->Release();
	pRenderTarget->Release();
	if (pStencilBuffer)
	{
		pStencilBuffer->Release();
	}

#ifdef _XBOX
	g_SPXBPhaseLast = 0x43425235; /* 'CBR5' */
#endif
}

static void dllRectd(GLdouble x1, GLdouble y1, GLdouble x2, GLdouble y2)
{
	assert(0);
}

static void dllRectdv(const GLdouble *v1, const GLdouble *v2)
{
	assert(0);
}

static void dllRectf(GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2)
{
	assert(0);
}

static void dllRectfv(const GLfloat *v1, const GLfloat *v2)
{
	assert(0);
}

static void dllRecti(GLint x1, GLint y1, GLint x2, GLint y2)
{
	assert(0);
}

static void dllRectiv(const GLint *v1, const GLint *v2)
{
	assert(0);
}

static void dllRects(GLshort x1, GLshort y1, GLshort x2, GLshort y2)
{
	assert(0);
}

static void dllRectsv(const GLshort *v1, const GLshort *v2)
{
	assert(0);
}

GLint dllRenderMode(GLenum mode)
{
	assert(0);
	return 0;
}

static void dllRotated(GLdouble angle, GLdouble x, GLdouble y, GLdouble z)
{
	assert(0);
}

static void dllRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
	D3DXVECTOR3 v(x, y, z);
	glw_state->matrixStack[glw_state->matrixMode]->RotateAxisLocal(&v, angle);
	glw_state->matricesDirty[glw_state->matrixMode] = true;
}

static void dllScaled(GLdouble x, GLdouble y, GLdouble z)
{
	assert(0);
}

static void dllScalef(GLfloat x, GLfloat y, GLfloat z)
{
	glw_state->matrixStack[glw_state->matrixMode]->Scale(x, y, z);
	glw_state->matricesDirty[glw_state->matrixMode] = true;
}

static void dllScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
#ifdef _XBOX
	_fixupScreenCoords(x, y, width, height);

	glw_state->scissorBox.x1 = x;
	glw_state->scissorBox.y1 = y;
	glw_state->scissorBox.x2 = x + width;
	glw_state->scissorBox.y2 = y + height;
	
	if (glw_state->scissorEnable)
	{
		glw_state->device->SetScissors(1, FALSE, &glw_state->scissorBox);
	}
#endif
}

static void dllSelectBuffer(GLsizei size, GLuint *buffer)
{
	assert(0);
}

static void dllShadeModel(GLenum mode)
{
	D3DSHADEMODE m;
	switch (mode)
	{
	case GL_FLAT: m = D3DSHADE_FLAT; break;
	case GL_SMOOTH: default: m = D3DSHADE_GOURAUD; break;
	}
	
	glw_state->device->SetRenderState(D3DRS_SHADEMODE, m);
}

static void dllStencilFunc(GLenum func, GLint ref, GLuint mask)
{
	D3DCMPFUNC f = _convertCompare(func);

	glw_state->device->SetRenderState(D3DRS_STENCILFUNC, f);
	glw_state->device->SetRenderState(D3DRS_STENCILREF, ref);
	glw_state->device->SetRenderState(D3DRS_STENCILMASK, mask);
}

static void dllStencilMask(GLuint mask)
{
	glw_state->device->SetRenderState(D3DRS_STENCILWRITEMASK, mask);
}

static D3DSTENCILOP _convertStencilOp(GLenum op)
{
	switch (op)
	{
	default: case GL_KEEP: return D3DSTENCILOP_KEEP;
	case GL_ZERO: return D3DSTENCILOP_ZERO;
	case GL_REPLACE: return D3DSTENCILOP_REPLACE;
	case GL_INCR: return D3DSTENCILOP_INCR;
	case GL_DECR: return D3DSTENCILOP_DECR;
	case GL_INVERT: return D3DSTENCILOP_INVERT;
	}
}

static void dllStencilOp(GLenum fail, GLenum zfail, GLenum zpass)
{
	D3DSTENCILOP f = _convertStencilOp(fail);
	D3DSTENCILOP zf = _convertStencilOp(zfail);
	D3DSTENCILOP zp = _convertStencilOp(zpass);

	glw_state->device->SetRenderState(D3DRS_STENCILFAIL, f);
	glw_state->device->SetRenderState(D3DRS_STENCILZFAIL, zf);
	glw_state->device->SetRenderState(D3DRS_STENCILPASS, zp);
}

static void dllTexCoord1d(GLdouble s)
{
	assert(0);
}

static void dllTexCoord1dv(const GLdouble *v)
{
	assert(0);
}

static void dllTexCoord1f(GLfloat s)
{
	assert(0);
}

static void dllTexCoord1fv(const GLfloat *v)
{
	assert(0);
}

static void dllTexCoord1i(GLint s)
{
	assert(0);
}

static void dllTexCoord1iv(const GLint *v)
{
	assert(0);
}

static void dllTexCoord1s(GLshort s)
{
	assert(0);
}

static void dllTexCoord1sv(const GLshort *v)
{
	assert(0);
}

static void setTexCoord(float s, float t)
{
	assert(glw_state->inDrawBlock);
	assert(glw_state->serverTU < GLW_MAX_TEXTURE_STAGES);
	s_immediateTexCoord[glw_state->serverTU][0] = s;
	s_immediateTexCoord[glw_state->serverTU][1] = t;
}

static void dllTexCoord2d(GLdouble s, GLdouble t)
{
	assert(0);
}

static void dllTexCoord2dv(const GLdouble *v)
{
	assert(0);
}

static void dllTexCoord2f(GLfloat s, GLfloat t)
{
	setTexCoord(s, t);
}

static void dllTexCoord2fv(const GLfloat *v)
{
	setTexCoord(v[0], v[1]);
}

static void dllTexCoord2i(GLint s, GLint t)
{
	assert(0);
}

static void dllTexCoord2iv(const GLint *v)
{
	assert(0);
}

static void dllTexCoord2s(GLshort s, GLshort t)
{
	assert(0);
}

static void dllTexCoord2sv(const GLshort *v)
{
	assert(0);
}

static void dllTexCoord3d(GLdouble s, GLdouble t, GLdouble r)
{
	assert(0);
}

static void dllTexCoord3dv(const GLdouble *v)
{
	assert(0);
}

static void dllTexCoord3f(GLfloat s, GLfloat t, GLfloat r)
{
	assert(0);
}

static void dllTexCoord3fv(const GLfloat *v)
{
	assert(0);
}

static void dllTexCoord3i(GLint s, GLint t, GLint r)
{
	assert(0);
}

static void dllTexCoord3iv(const GLint *v)
{
	assert(0);
}

static void dllTexCoord3s(GLshort s, GLshort t, GLshort r)
{
	assert(0);
}

static void dllTexCoord3sv(const GLshort *v)
{
	assert(0);
}

static void dllTexCoord4d(GLdouble s, GLdouble t, GLdouble r, GLdouble q)
{
	assert(0);
}

static void dllTexCoord4dv(const GLdouble *v)
{
	assert(0);
}

static void dllTexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q)
{
	assert(0);
}

static void dllTexCoord4fv(const GLfloat *v)
{
	assert(0);
}

static void dllTexCoord4i(GLint s, GLint t, GLint r, GLint q)
{
	assert(0);
}

static void dllTexCoord4iv(const GLint *v)
{
	assert(0);
}

static void dllTexCoord4s(GLshort s, GLshort t, GLshort r, GLshort q)
{
	assert(0);
}

static void dllTexCoord4sv(const GLshort *v)
{
	assert(0);
}

static void dllTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
	assert(size == 2 && type == GL_FLOAT);

	stride = (stride == 0) ? (sizeof(GLfloat) * 2) : stride;
	
	glw_state->texCoordPointer[glw_state->clientTU] = pointer;
	glw_state->texCoordStride[glw_state->clientTU] = stride;
}

static void dllTexEnvf(GLenum target, GLenum pname, GLfloat param)
{
	qglTexEnvi(target, pname, (GLint)param);
}

static void dllTexEnvfv(GLenum target, GLenum pname, const GLfloat *params)
{
	assert(0);
}

static void dllTexEnvi(GLenum target, GLenum pname, GLint param)
{
	assert(target == GL_TEXTURE_ENV && pname == GL_TEXTURE_ENV_MODE);

	/*glwstate_t::TextureInfo* info = _getCurrentTexture(glw_state->serverTU);
	if (!info) return;*/

	D3DTEXTUREOP env;
	switch (param)
	{
	case GL_MODULATE: default: env = D3DTOP_MODULATE; break;
	case GL_REPLACE: env = D3DTOP_SELECTARG1; break;
	// MATT! - I use GL_DECAL as the bumpmapping state
	case GL_DECAL: env = D3DTOP_DOTPRODUCT3; break;
	case GL_ADD: env = D3DTOP_ADD; break;
	case GL_NONE: env = D3DTOP_DISABLE; break;
	}

	if (glw_state->textureEnv[glw_state->serverTU] != env)
	{
		glw_state->textureEnv[glw_state->serverTU] = env;
		glw_state->textureStageDirty[glw_state->serverTU] = true;
	}
}

static void dllTexEnviv(GLenum target, GLenum pname, const GLint *params)
{
	assert(0);
}

static void dllTexGend(GLenum coord, GLenum pname, GLdouble param)
{
	assert(0);
}

static void dllTexGendv(GLenum coord, GLenum pname, const GLdouble *params)
{
	assert(0);
}

static void dllTexGenf(GLenum coord, GLenum pname, GLfloat param)
{
	assert(0);
}

static void dllTexGenfv(GLenum coord, GLenum pname, const GLfloat *params)
{
	assert(0);
}

static void dllTexGeni(GLenum coord, GLenum pname, GLint param)
{
	assert(0);
}

static void dllTexGeniv(GLenum coord, GLenum pname, const GLint *params)
{
	assert(0);
}

static void dllTexImage1D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
	assert(0);
}

static void _swizzleRectCPU(const void* source, void* destination,
	DWORD width, DWORD height, DWORD bytesPerPixel)
{
	assert((width & (width - 1)) == 0);
	assert((height & (height - 1)) == 0);
	assert(bytesPerPixel == 2 || bytesPerPixel == 4);

	XGSwizzleRect(source, 0, NULL, destination,
		width, height, NULL, bytesPerPixel);
}

static qboolean _uploadSwizzledTextureLevel(IDirect3DTexture8* texture,
	DWORD level, const void* source, DWORD width, DWORD height,
	DWORD bytesPerPixel)
{
	IDirect3DSurface8* surface = NULL;
	D3DLOCKED_RECT locked;

	if (!texture || !source)
		return qfalse;

	if (FAILED(texture->GetSurfaceLevel(level, &surface)) || !surface)
		return qfalse;

	const HRESULT lockResult = surface->LockRect(&locked, NULL, 0);
	if (FAILED(lockResult) || !locked.pBits)
	{
		surface->Release();
		return qfalse;
	}

	_swizzleRectCPU(source, locked.pBits, width, height, bytesPerPixel);
	surface->UnlockRect();
	surface->Release();
	return qtrue;
}

static void _texImageDDS(glwstate_t::TextureInfo* info, GLint numlevels, GLsizei width, GLsizei height, GLenum format, const GLvoid *pixels)
{
	D3DFORMAT f = D3DFMT_UNKNOWN;
	switch( format )
	{
	case GL_DDS1_EXT:
		f = D3DFMT_DXT1;
		break;
	case GL_DDS5_EXT:
		f = D3DFMT_DXT5;
		break;
	case GL_DDS_RGB16_EXT:
		f = D3DFMT_R5G6B5;
		break;
	case GL_DDS_RGBA32_EXT:
		f = D3DFMT_A8R8G8B8;
		break;
	}

	if( numlevels == 0)
		numlevels = 1;

	const DWORD headerSize = sizeof(DWORD) + sizeof(DDS_HEADER);
	const DWORD fileSize = pixels ? Z_Size(const_cast<void*>(pixels)) : 0;
	const qboolean hasDDSHeader =
		pixels &&
		fileSize >= headerSize &&
		*(const DWORD*)pixels == MAKEFOURCC('D', 'D', 'S', ' ');
	const qboolean isSaveGamePlaceholder =
		pixels &&
		!hasDDSHeader &&
		format == GL_DDS1_EXT &&
		width == SAVE_GAME_IMAGE_W &&
		height == SAVE_GAME_IMAGE_H;

	if (!hasDDSHeader && !isSaveGamePlaceholder)
	{
		g_SPXBPhaseLast = 0x44444648; /* 'DDFH' */
		Com_Error(ERR_FATAL,
			"Native D3D8 DDS upload received an invalid container: format=0x%04x size=%u dims=%dx%d",
			(unsigned)format, (unsigned)fileSize, (int)width, (int)height);
		return;
	}

	info->mipmap = new IDirect3DTexture8;
	DWORD pixelSize = XGSetTextureHeader( width,
						height,
						numlevels,
						(D3DPOOL)0,
						f,
						(D3DPOOL)0,
						info->mipmap,
						(D3DPOOL)0,
						0 );

	const byte* sourceData = hasDDSHeader
		? (const byte*)pixels + headerSize
		: NULL;
	const DWORD sourceBytes = hasDDSHeader ? fileSize - headerSize : 0;
	if (!info->data || info->dataSize < pixelSize)
	{
		info->data = gTextures.Allocate(pixelSize,
			glw_state->currentTexture[glw_state->serverTU]);
		info->dataSize = info->data ? pixelSize : 0;
	}
	if (!info->data)
	{
		g_SPXBPhaseLast = 0x44444641; /* 'DDFA' */
		Com_Error(ERR_FATAL,
			"Native D3D8 DDS texture pool exhausted: request=%u used=%u capacity=%u",
			(unsigned)pixelSize, (unsigned)gTextures.Size(),
			(unsigned)gTextures.Capacity());
		return;
	}

	memset(info->data, 0, pixelSize);
	info->mipmap->Register(info->data);

	// R_CreateBuiltinImages deliberately reserves the savegame slot as
	// DXT1 before an XPR screenshot exists. A zeroed DXT1 allocation is a
	// valid black placeholder and preserves the format expected by the
	// later direct XPR payload copy.
	if (isSaveGamePlaceholder)
	{
		XBL("Native D3D8: initialized blank DXT1 savegame placeholder\n");
		return;
	}

	// DDS files store uncompressed pixels in linear row order. Xbox
	// A8R8G8B8 and R5G6B5 textures are swizzled in registered memory.
	if( f == D3DFMT_R5G6B5 || f == D3DFMT_A8R8G8B8 )
	{
		const DWORD bytesPerPixel = f == D3DFMT_R5G6B5 ? 2 : 4;
		const byte *pSrc = sourceData;
		DWORD remainingSourceBytes = sourceBytes;
		DWORD level = numlevels;
		DWORD curWidth = width;
		DWORD curHeight = height;
		DWORD levelIndex = 0;
		while (level--)
		{
			const DWORD levelBytes = curWidth * curHeight * bytesPerPixel;
			if (remainingSourceBytes < levelBytes ||
				!_uploadSwizzledTextureLevel(info->mipmap, levelIndex,
					pSrc, curWidth, curHeight, bytesPerPixel))
			{
				g_SPXBPhaseLast = 0x44444655; /* 'DDFU' */
				Com_Error(ERR_FATAL,
					"Native D3D8 DDS upload failed: level=%u size=%ux%u source=%u",
					(unsigned)levelIndex, (unsigned)curWidth,
					(unsigned)curHeight, (unsigned)remainingSourceBytes);
				return;
			}
			pSrc += levelBytes;
			remainingSourceBytes -= levelBytes;
			++levelIndex;
			if (curWidth > 1)
				curWidth >>= 1;
			if (curHeight > 1)
				curHeight >>= 1;
		}
	}
	else
	{
		const DWORD copyBytes = sourceBytes < pixelSize ? sourceBytes : pixelSize;
		memcpy(info->data, sourceData, copyBytes);
	}
}

static void _downsampleBGRAInPlace(DWORD* pixels, DWORD width, DWORD height)
{
	const DWORD nextWidth = width > 1 ? width >> 1 : 1;
	const DWORD nextHeight = height > 1 ? height >> 1 : 1;

	for (DWORD y = 0; y < nextHeight; ++y)
	{
		const DWORD y0 = y << 1;
		const DWORD y1 = y0 + 1 < height ? y0 + 1 : y0;

		for (DWORD x = 0; x < nextWidth; ++x)
		{
			const DWORD x0 = x << 1;
			const DWORD x1 = x0 + 1 < width ? x0 + 1 : x0;
			const DWORD p0 = pixels[y0 * width + x0];
			const DWORD p1 = pixels[y0 * width + x1];
			const DWORD p2 = pixels[y1 * width + x0];
			const DWORD p3 = pixels[y1 * width + x1];

			const DWORD b = ((p0 & 0xff) + (p1 & 0xff) +
				(p2 & 0xff) + (p3 & 0xff) + 2) >> 2;
			const DWORD g = (((p0 >> 8) & 0xff) + ((p1 >> 8) & 0xff) +
				((p2 >> 8) & 0xff) + ((p3 >> 8) & 0xff) + 2) >> 2;
			const DWORD r = (((p0 >> 16) & 0xff) + ((p1 >> 16) & 0xff) +
				((p2 >> 16) & 0xff) + ((p3 >> 16) & 0xff) + 2) >> 2;
			const DWORD a = (((p0 >> 24) & 0xff) + ((p1 >> 24) & 0xff) +
				((p2 >> 24) & 0xff) + ((p3 >> 24) & 0xff) + 2) >> 2;

			pixels[y * nextWidth + x] = b | (g << 8) | (r << 16) | (a << 24);
		}
	}
}

static qboolean _uploadUncompressedMipChain(IDirect3DTexture8* texture,
	GLint numlevels, GLsizei width, GLsizei height, D3DFORMAT dstFormat,
	GLenum srcFormat, const GLvoid* pixels)
{
	const DWORD pixelCount = width * height;
	g_SPXBPhaseLast = 0x54443031; /* 'TD01' */
	DWORD* bgra = (DWORD*)Z_Malloc(pixelCount * sizeof(DWORD),
		TAG_TEMP_WORKSPACE, qfalse, 4);
	g_SPXBPhaseLast = 0x54443032; /* 'TD02' */
	byte* encoded = (byte*)Z_Malloc(pixelCount * sizeof(DWORD),
		TAG_TEMP_WORKSPACE, qfalse, 4);
	g_SPXBPhaseLast = 0x54443033; /* 'TD03' */
	const byte* src = (const byte*)pixels;
	DWORD bytesPerPixel;

	if (!texture || !bgra || !encoded)
	{
		g_SPXBPhaseLast = 0x54444630; /* 'TDF0' */
		if (encoded)
			Z_Free(encoded);
		if (bgra)
			Z_Free(bgra);
		return qfalse;
	}

	if (srcFormat == GL_RGBA)
	{
		for (DWORD i = 0; i < pixelCount; ++i)
		{
			const byte* rgba = src + i * 4;
			bgra[i] = rgba[2] | (rgba[1] << 8) | (rgba[0] << 16) |
				(rgba[3] << 24);
		}
	}
	else if (srcFormat == GL_RGB)
	{
		for (DWORD i = 0; i < pixelCount; ++i)
		{
			const byte* rgb = src + i * 3;
			bgra[i] = rgb[2] | (rgb[1] << 8) | (rgb[0] << 16) | 0xff000000;
		}
	}
	else if (srcFormat == GL_LIN_RGBA || srcFormat == GL_LIN_RGB ||
		srcFormat == GL_LIN_RGB8 || srcFormat == GL_RGB8)
	{
		memcpy(bgra, pixels, pixelCount * sizeof(DWORD));
	}
	else
	{
		g_SPXBPhaseLast = 0x54444631; /* 'TDF1' */
		Z_Free(encoded);
		Z_Free(bgra);
		return qfalse;
	}
	g_SPXBPhaseLast = 0x54443034; /* 'TD04' */

	if (dstFormat == D3DFMT_A8R8G8B8 || dstFormat == D3DFMT_X8R8G8B8)
		bytesPerPixel = 4;
	else if (dstFormat == D3DFMT_R5G6B5 || dstFormat == D3DFMT_A4R4G4B4)
		bytesPerPixel = 2;
	else
	{
		g_SPXBPhaseLast = 0x54444632; /* 'TDF2' */
		Z_Free(encoded);
		Z_Free(bgra);
		return qfalse;
	}
	g_SPXBPhaseLast = 0x54443035; /* 'TD05' */

	DWORD levelWidth = width;
	DWORD levelHeight = height;
	for (DWORD level = 0; level < (DWORD)numlevels; ++level)
	{
		const DWORD levelPixels = levelWidth * levelHeight;
		g_SPXBPhaseLast = 0x54443130; /* 'TD10' */

		if (bytesPerPixel == 4)
		{
			memcpy(encoded, bgra, levelPixels * sizeof(DWORD));
		}
		else
		{
			WORD* encoded16 = (WORD*)encoded;
			for (DWORD i = 0; i < levelPixels; ++i)
			{
				const DWORD pixel = bgra[i];
				const DWORD b = pixel & 0xff;
				const DWORD g = (pixel >> 8) & 0xff;
				const DWORD r = (pixel >> 16) & 0xff;
				const DWORD a = (pixel >> 24) & 0xff;

				if (dstFormat == D3DFMT_R5G6B5)
					encoded16[i] = (WORD)(((r >> 3) << 11) |
						((g >> 2) << 5) | (b >> 3));
				else
					encoded16[i] = (WORD)(((a >> 4) << 12) |
						((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4));
			}
		}

		g_SPXBPhaseLast = 0x54443131; /* 'TD11' */
		if (!_uploadSwizzledTextureLevel(texture, level, encoded,
			levelWidth, levelHeight, bytesPerPixel))
		{
			g_SPXBPhaseLast = 0x54444633; /* 'TDF3' */
			Z_Free(encoded);
			Z_Free(bgra);
			return qfalse;
		}
		g_SPXBPhaseLast = 0x54443132; /* 'TD12' */

		if (level + 1 < (DWORD)numlevels)
		{
			_downsampleBGRAInPlace(bgra, levelWidth, levelHeight);
			g_SPXBPhaseLast = 0x54443138; /* 'TD18' */
			if (levelWidth > 1)
				levelWidth >>= 1;
			if (levelHeight > 1)
				levelHeight >>= 1;
		}
	}

	Z_Free(encoded);
	Z_Free(bgra);
	g_SPXBPhaseLast = 0x54443139; /* 'TD19' */
	return qtrue;
}

static void _texImageRGBA(glwstate_t::TextureInfo* info, GLint numlevels, GLint internalformat, GLsizei width, GLsizei height, GLenum format, const GLvoid *pixels)
{
	g_SPXBPhaseLast = 0x54583030; /* 'TX00' */
	// Fix number of levels:
	if( numlevels == 0 )
		numlevels = 1;

	// What format should the resultant texture be:
	D3DFORMAT dstFormat = D3DFMT_UNKNOWN;
	switch(internalformat)
	{
		case GL_RGB5:
		case GL_RGB4_S3TC:
			dstFormat = D3DFMT_R5G6B5;
			break;

		case GL_RGBA4:
			dstFormat = D3DFMT_A4R4G4B4;
			break;

		case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
			dstFormat = D3DFMT_DXT1;
			break;

		case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
			dstFormat = D3DFMT_DXT5;
			break;

		case GL_RGB8:
		case 3:
			dstFormat = D3DFMT_X8R8G8B8;
			break;

		case GL_LIN_RGBA8:
			dstFormat = D3DFMT_LIN_A8R8G8B8;
			break;
		case GL_LIN_RGB8:
			dstFormat = D3DFMT_LIN_X8R8G8B8;
			break;

		case GL_RGBA8:
		case 4:
			dstFormat = D3DFMT_A8R8G8B8;
			break;
		case GL_RGB:
			dstFormat = D3DFMT_X8R8G8B8;
			break;

		default:
			assert(0);
	}

	// Runtime assets arrive as precompressed DDS. Small engine-generated
	// textures must not enter the XDK D3DX compressor/filter path.
	if (dstFormat == D3DFMT_DXT1)
		dstFormat = D3DFMT_R5G6B5;
	else if (dstFormat == D3DFMT_DXT5)
		dstFormat = D3DFMT_A8R8G8B8;
	else if (dstFormat == D3DFMT_LIN_A8R8G8B8)
		dstFormat = D3DFMT_A8R8G8B8;
	else if (dstFormat == D3DFMT_LIN_X8R8G8B8)
		dstFormat = D3DFMT_X8R8G8B8;

	// What format is our source data in:
	D3DFORMAT srcFormat = D3DFMT_UNKNOWN;
	float bpp;
	int pitch;
	switch(format)
	{
		case GL_RGB:
			srcFormat = D3DFMT_X8R8G8B8;
			bpp = 3;
			break;
			
		case GL_RGBA:
			srcFormat = D3DFMT_A8R8G8B8;
			bpp = 4;
			break;

		case GL_LIN_RGBA:
			srcFormat = D3DFMT_LIN_A8R8G8B8;
			bpp = 4;
			break;

		case GL_LIN_RGB:
			srcFormat = D3DFMT_LIN_X8R8G8B8;
			bpp = 4;
			break;

		case GL_LIN_RGB8:
			srcFormat = D3DFMT_LIN_X8R8G8B8;
			bpp = 4;
			break;

		case GL_RGB8:
			srcFormat = D3DFMT_X8R8G8B8;
			bpp = 4;
			break;

		case GL_RGB_SWIZZLE_EXT:
			srcFormat = D3DFMT_R5G6B5;
			bpp = 2;
			break;

		case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
			srcFormat = D3DFMT_DXT1;
			bpp = 0.5;
			break;

		default:
			assert(0);
	}

	pitch = (int)((float)width * bpp);
	g_SPXBPhaseLast = 0x54583031; /* 'TX01' */

	RECT	srcRect;

	srcRect.top = 0;
	srcRect.left = 0;
	srcRect.right = width;
	srcRect.bottom = height;

	info->mipmap = new IDirect3DTexture8;
	g_SPXBPhaseLast = 0x54583032; /* 'TX02' */
	DWORD pixelSize = XGSetTextureHeader( width,
						height,
						numlevels,
						0,
						dstFormat,
						(D3DPOOL)0,
						info->mipmap,
						0,
						0 );
	g_SPXBPhaseLast = 0x54583033; /* 'TX03' */

	if (!info->data || info->dataSize < pixelSize)
	{
		info->data = gTextures.Allocate(pixelSize,
			glw_state->currentTexture[glw_state->serverTU]);
		info->dataSize = info->data ? pixelSize : 0;
	}
	g_SPXBPhaseLast = 0x54583034; /* 'TX04' */
	if (!info->data)
	{
		g_SPXBPhaseLast = 0x54584641; /* 'TXFA' */
		Com_Error(ERR_FATAL,
			"Native D3D8 texture pool exhausted: request=%u used=%u capacity=%u",
			(unsigned)pixelSize, (unsigned)gTextures.Size(),
			(unsigned)gTextures.Capacity());
		return;
	}

	memset(info->data, 0, pixelSize);
	info->mipmap->Register(info->data);
	g_SPXBPhaseLast = 0x54584430; /* 'TXD0' */
	if (_uploadUncompressedMipChain(info->mipmap, numlevels, width, height,
		dstFormat, format, pixels))
	{
		g_SPXBPhaseLast = 0x54583035; /* 'TX05' */
		g_SPXBPhaseLast = 0x54584439; /* 'TXD9' */
		return;
	}

	// Compressed on-the-fly conversions are legacy-only. Runtime assets use DDS.
	g_SPXBPhaseLast = 0x54584630; /* 'TXF0' */
	Com_Error(ERR_FATAL,
		"Native D3D8 generated texture format unsupported: internal=0x%x source=0x%x size=%dx%d",
		internalformat, format, width, height);
	return;

	info->mipmap->Register( info->data );
	g_SPXBPhaseLast = 0x54584631; /* 'TXF1' */
	IDirect3DSurface8 *pSurf = NULL;
	info->mipmap->GetSurfaceLevel( 0, &pSurf );
	g_SPXBPhaseLast = 0x54583036; /* 'TX06' */

	D3DXLoadSurfaceFromMemory( pSurf,
							   NULL,
							   NULL,
							   pixels,
							   srcFormat,
							   pitch,
							   NULL,
							   &srcRect,
							   D3DX_DEFAULT,
							   0 );
	g_SPXBPhaseLast = 0x54583037; /* 'TX07' */

	pSurf->Release();
	g_SPXBPhaseLast = 0x54583038; /* 'TX08' */

	// Generate mipmaps
	if( numlevels > 1)
	{
		D3DXFilterTexture( info->mipmap,
						   NULL,
						   D3DX_DEFAULT,
						   D3DX_DEFAULT );
	}
	g_SPXBPhaseLast = 0x54583039; /* 'TX09' */
}

// EXTENSION: glTexImage2D plus "numlevels" number of mipmaps
static void dllTexImage2DEXT(GLenum target, GLint level, GLint numlevels, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
	assert(target == GL_TEXTURE_2D && border == 0 && type == GL_UNSIGNED_BYTE);

	// In Direct3D, setting 0 for number of mipmap 
	// levels means create the whole chain....
	/*if(numlevels == 0)
		numlevels = 1;*/

	glwstate_t::TextureInfo* info;

	glwstate_t::texturexlat_t::iterator current = 
		glw_state->textureXlat.find(
		glw_state->currentTexture[glw_state->serverTU]);

	// If we already have a texture bound to this ID, remove it.
	if (current != glw_state->textureXlat.end())
	{
		info = &current->second;

		glw_state->device->SetTexture(glw_state->serverTU, NULL);
		glw_state->textureStageDirty[glw_state->serverTU] = true;
		if (info->mipmap)
		{
#if MEMORY_PROFILE
			texMemSize -= getTexMemSize(info->mipmap);
#endif
			info->mipmap->BlockUntilNotBusy();
			delete info->mipmap;
			info->mipmap = NULL;
		}
		if (info->data)
		{
			if (!gTextures.Free(info->data, info->dataSize))
			{
				g_SPXBPhaseLast = 0x54324652; /* 'T2FR' */
				Com_Error(ERR_FATAL,
					"Native D3D8 texture replacement could not release old allocation");
				return;
			}
			info->data = NULL;
			info->dataSize = 0;
		}
	}
	// Otherwise, initialize it.
	else
	{
		info = &glw_state->textureXlat[
			glw_state->currentTexture[glw_state->serverTU]];
		memset(info, 0, sizeof(*info));

		info->minFilter = D3DTEXF_NONE;
		info->mipFilter = D3DTEXF_NONE;
		info->magFilter = D3DTEXF_NONE;
		info->anisotropy = 1.f;
		info->wrapU = D3DTADDRESS_CLAMP;
		info->wrapV = D3DTADDRESS_CLAMP;

		glw_state->textureStageDirty[glw_state->serverTU] = true;
	}

	// force any DX allocs to temp memory
//	Z_SetNewDeleteTemporary(true);

	if (format == GL_DDS1_EXT || 
		format == GL_DDS5_EXT || 
		format == GL_DDS_RGB16_EXT || 
		format == GL_DDS_RGBA32_EXT)
	{
		_texImageDDS(info, numlevels, width, height, format, pixels);
	}
	else
	{
		_texImageRGBA(info, numlevels, 
			internalformat, width, height, 
			format, pixels); 
	}
	g_SPXBPhaseLast = 0x54324439; /* 'T2D9' */

	// Done DX calls to new and delete
//	Z_SetNewDeleteTemporary(false);

#if MEMORY_PROFILE
	texMemSize += getTexMemSize(info->mipmap);
#endif
}

static void dllTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
	dllTexImage2DEXT(target, level, 1, internalformat, width, height, border, format, type, pixels);
}

static void dllTexParameteri(GLenum target, GLenum pname, GLint param)
{
	assert(target == GL_TEXTURE_2D);
	
	if (glw_state->currentTexture[glw_state->serverTU] == 0) return;

	glwstate_t::TextureInfo* info = _getCurrentTexture(glw_state->serverTU);
	if (!info) return;

	glw_state->textureStageDirty[glw_state->serverTU] = true;
	
	switch (pname)
	{
	case GL_TEXTURE_MIN_FILTER:
		switch (param)
		{
		case GL_NEAREST:
			info->minFilter = D3DTEXF_POINT;
			info->mipFilter = D3DTEXF_NONE;
			break;
		case GL_LINEAR:
			info->minFilter = D3DTEXF_LINEAR;
			info->mipFilter = D3DTEXF_NONE;
			break;
		case GL_NEAREST_MIPMAP_NEAREST:
			info->minFilter = D3DTEXF_POINT;
			info->mipFilter = D3DTEXF_POINT;
			break;
		case GL_LINEAR_MIPMAP_NEAREST:
			info->minFilter = D3DTEXF_LINEAR;
			info->mipFilter = D3DTEXF_POINT;
			break;
		case GL_NEAREST_MIPMAP_LINEAR:
			info->minFilter = D3DTEXF_POINT;
			info->mipFilter = D3DTEXF_LINEAR;
			break;
		case GL_LINEAR_MIPMAP_LINEAR:
			info->minFilter = D3DTEXF_LINEAR;
			info->mipFilter = D3DTEXF_LINEAR;
			break;
		}
		info->anisotropy = 1.f;
		break;
	case GL_TEXTURE_MAG_FILTER:
		switch (param)
		{
		case GL_NEAREST:
			info->magFilter = D3DTEXF_POINT;
			break;
		case GL_LINEAR:
			info->magFilter = D3DTEXF_LINEAR;
			break;
		}
		info->anisotropy = 1.f;
		break;
	case GL_TEXTURE_WRAP_S:
		switch (param)
		{
		case GL_REPEAT: info->wrapU = D3DTADDRESS_WRAP; break;
		case GL_CLAMP: info->wrapU = D3DTADDRESS_CLAMP; break;
		}
		break;
	case GL_TEXTURE_WRAP_T:
		switch (param)
		{
		case GL_REPEAT: info->wrapV = D3DTADDRESS_WRAP; break;
		case GL_CLAMP: info->wrapV = D3DTADDRESS_CLAMP; break;
		}
		break;
	case GL_TEXTURE_MAX_ANISOTROPY_EXT:
		info->anisotropy = (float)param;
		info->minFilter = D3DTEXF_ANISOTROPIC;
		info->magFilter = D3DTEXF_ANISOTROPIC;
		break;
	}
}

static void dllTexParameterf(GLenum target, GLenum pname, GLfloat param)
{
	dllTexParameteri(target, pname, param);
}

static void dllTexParameterfv(GLenum target, GLenum pname, const GLfloat *params)
{
	// Intentionally left blank
}

static void dllTexParameteriv(GLenum target, GLenum pname, const GLint *params)
{
	// Intentionally left blank
}

static void dllTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type, const GLvoid *pixels)
{
	assert(0);
}

static void dllTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels)
{
	assert(target == GL_TEXTURE_2D && level == 0 && type == GL_UNSIGNED_BYTE);

	glwstate_t::TextureInfo* info = _getCurrentTexture(glw_state->serverTU);
	if (info == NULL) return;

	RECT sr;
	sr.top = 0;
	sr.left = 0;
	sr.right = width;
	sr.bottom = height;

	RECT dr;
	dr.left = xoffset;
	dr.top = yoffset;
	dr.right = xoffset + width;
	dr.bottom = yoffset + height;

	Z_SetNewDeleteTemporary(true);

	LPDIRECT3DSURFACE8 surf;
	info->mipmap->GetSurfaceLevel(0, &surf);
	
	// We use the supplied format to handle pixel data correctly, the way OGL would
	D3DFORMAT srcFormat;
	switch(format)
	{
		case GL_RGB:
			srcFormat = D3DFMT_LIN_X8R8G8B8;
			break;

		case GL_RGBA:
			srcFormat = D3DFMT_LIN_A8R8G8B8;
			break;

		default:
			assert(0 && "Unsupported format in dllTexSubImage2D");
			return;
	}

	D3DXLoadSurfaceFromMemory(surf, NULL, &dr, pixels,
		srcFormat, width * 4, NULL, &sr, D3DX_DEFAULT, 0);

	surf->Release();

	Z_SetNewDeleteTemporary(false);
}

static void dllTranslated(GLdouble x, GLdouble y, GLdouble z)
{
	assert(0);
}

static void dllTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
	glw_state->matrixStack[glw_state->matrixMode]->TranslateLocal(x, y, z);
	glw_state->matricesDirty[glw_state->matrixMode] = true;
}

static void setVertex(float x, float y, float z)
{
	assert(glw_state->inDrawBlock);
	if (!glw_state || !glw_state->inDrawBlock || !glw_state->drawArray ||
		glw_state->numVertices >= glw_state->maxVertices)
	{
		g_SPXBPhaseLast = 0x494D4633; /* 'IMF3' */
		Com_Error(ERR_DROP, "Native D3D8 immediate vertex overflow or invalid state");
		return;
	}

	_handleDrawOverflow();
	
	DWORD* push = &glw_state->drawArray[glw_state->numVertices * glw_state->drawStride];
	memcpy(push, &x, sizeof(x));
	memcpy(push + 1, &y, sizeof(y));
	memcpy(push + 2, &z, sizeof(z));
	push += 3;

	if (s_immediateHasNormal)
	{
		memcpy(push, s_immediateNormal, sizeof(s_immediateNormal));
		push += 3;
	}

	*push++ = glw_state->currentColor;
	for (int stage = 0; stage < GLW_MAX_TEXTURE_STAGES; ++stage)
	{
		if (s_immediateHasTexCoord[stage])
		{
			memcpy(push, s_immediateTexCoord[stage],
				sizeof(s_immediateTexCoord[stage]));
			push += 2;
		}
	}
	assert(push == &glw_state->drawArray[
		(glw_state->numVertices + 1) * glw_state->drawStride]);

	++glw_state->numVertices;
}

static void dllVertex2d(GLdouble x, GLdouble y)
{
	assert(0);
}

static void dllVertex2dv(const GLdouble *v)
{
	assert(0);
}

static void dllVertex2f(GLfloat x, GLfloat y)
{
	setVertex(x, y, 0.f);
}

static void dllVertex2fv(const GLfloat *v)
{
	setVertex(v[0], v[1], 0.f);
}

static void dllVertex2i(GLint x, GLint y)
{
	assert(0);
}

static void dllVertex2iv(const GLint *v)
{
	assert(0);
}

static void dllVertex2s(GLshort x, GLshort y)
{
	assert(0);
}

static void dllVertex2sv(const GLshort *v)
{
	assert(0);
}

static void dllVertex3d(GLdouble x, GLdouble y, GLdouble z)
{
	assert(0);
}

static void dllVertex3dv(const GLdouble *v)
{
	assert(0);
}

static void dllVertex3f(GLfloat x, GLfloat y, GLfloat z)
{
	setVertex(x, y, z);
}

static void dllVertex3fv(const GLfloat *v)
{
	setVertex(v[0], v[1], v[2]);
}

static void dllVertex3i(GLint x, GLint y, GLint z)
{
	assert(0);
}

static void dllVertex3iv(const GLint *v)
{
	assert(0);
}

static void dllVertex3s(GLshort x, GLshort y, GLshort z)
{
	assert(0);
}

static void dllVertex3sv(const GLshort *v)
{
	assert(0);
}

static void dllVertex4d(GLdouble x, GLdouble y, GLdouble z, GLdouble w)
{
	assert(0);
}

static void dllVertex4dv(const GLdouble *v)
{
	assert(0);
}

static void dllVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
	setVertex(x, y, z);
}

static void dllVertex4fv(const GLfloat *v)
{
	setVertex(v[0], v[1], v[2]);
}

static void dllVertex4i(GLint x, GLint y, GLint z, GLint w)
{
	assert(0);
}

static void dllVertex4iv(const GLint *v)
{
	assert(0);
}

static void dllVertex4s(GLshort x, GLshort y, GLshort z, GLshort w)
{
	assert(0);
}

static void dllVertex4sv(const GLshort *v)
{
	assert(0);
}

static void dllVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
	assert(size == 3 && type == GL_FLOAT);
	
	stride = (stride == 0) ? (sizeof(GLfloat) * 3) : stride;

	glw_state->vertexPointer = pointer;
	glw_state->vertexStride = stride;
}

static void dllViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
	_fixupScreenCoords(x, y, width, height);

	glw_state->viewport.X = x;
	glw_state->viewport.Y = y;
	glw_state->viewport.Width = width;
	glw_state->viewport.Height = height;
	glw_state->device->SetViewport(&glw_state->viewport);
}


static void dllMultiTexCoord2fARB(GLenum texture, GLfloat s, GLfloat t)
{
	assert(glw_state->inDrawBlock);
	const int stage = texture - GL_TEXTURE0_ARB;
	assert(stage >= 0 && stage < GLW_MAX_TEXTURE_STAGES);
	s_immediateTexCoord[stage][0] = s;
	s_immediateTexCoord[stage][1] = t;
}

static void dllActiveTextureARB(GLenum texture)
{
	assert(GLW_MAX_TEXTURE_STAGES > texture - GL_TEXTURE0_ARB);
	glw_state->serverTU = texture - GL_TEXTURE0_ARB;
}

static void dllClientActiveTextureARB(GLenum texture)
{
	assert(GLW_MAX_TEXTURE_STAGES > texture - GL_TEXTURE0_ARB);
	glw_state->clientTU = texture - GL_TEXTURE0_ARB;
}


/*
** QGL_Shutdown
**
** Unloads the specified DLL then nulls out all the proc pointers.  This
** is only called during a hard shutdown of the OGL subsystem (e.g. vid_restart).
*/
void QGL_Shutdown( void )
{
	VID_Printf( PRINT_ALL, "...shutting down QGL\n" );

	qglAccum                     = NULL;
	qglAlphaFunc                 = NULL;
	qglAreTexturesResident       = NULL;
	qglArrayElement              = NULL;
	qglBegin                     = NULL;
	qglBeginEXT                  = NULL;
	qglBeginFrame                = NULL;
	qglBeginShadow               = NULL;
	qglBindTexture               = NULL;
	qglBitmap                    = NULL;
	qglBlendFunc                 = NULL;
	qglCallList                  = NULL;
	qglCallLists                 = NULL;
	qglClear                     = NULL;
	qglClearAccum                = NULL;
	qglClearColor                = NULL;
	qglClearDepth                = NULL;
	qglClearIndex                = NULL;
	qglClearStencil              = NULL;
	qglClipPlane                 = NULL;
	qglColor3b                   = NULL;
	qglColor3bv                  = NULL;
	qglColor3d                   = NULL;
	qglColor3dv                  = NULL;
	qglColor3f                   = NULL;
	qglColor3fv                  = NULL;
	qglColor3i                   = NULL;
	qglColor3iv                  = NULL;
	qglColor3s                   = NULL;
	qglColor3sv                  = NULL;
	qglColor3ub                  = NULL;
	qglColor3ubv                 = NULL;
	qglColor3ui                  = NULL;
	qglColor3uiv                 = NULL;
	qglColor3us                  = NULL;
	qglColor3usv                 = NULL;
	qglColor4b                   = NULL;
	qglColor4bv                  = NULL;
	qglColor4d                   = NULL;
	qglColor4dv                  = NULL;
	qglColor4f                   = NULL;
	qglColor4fv                  = NULL;
	qglColor4i                   = NULL;
	qglColor4iv                  = NULL;
	qglColor4s                   = NULL;
	qglColor4sv                  = NULL;
	qglColor4ub                  = NULL;
	qglColor4ubv                 = NULL;
	qglColor4ui                  = NULL;
	qglColor4uiv                 = NULL;
	qglColor4us                  = NULL;
	qglColor4usv                 = NULL;
	qglColorMask                 = NULL;
	qglColorMaterial             = NULL;
	qglColorPointer              = NULL;
	qglCopyPixels                = NULL;
	qglCopyTexImage1D            = NULL;
	qglCopyTexImage2D            = NULL;
	qglCopyTexSubImage1D         = NULL;
	qglCopyTexSubImage2D         = NULL;
	qglCullFace                  = NULL;
	qglDeleteLists               = NULL;
	qglDeleteTextures            = NULL;
	qglDepthFunc                 = NULL;
	qglDepthMask                 = NULL;
	qglDepthRange                = NULL;
	qglDisable                   = NULL;
	qglDisableClientState        = NULL;
	qglDrawArrays                = NULL;
	qglDrawBuffer                = NULL;
	qglDrawElements              = NULL;
	qglDrawPixels                = NULL;
	qglEdgeFlag                  = NULL;
	qglEdgeFlagPointer           = NULL;
	qglEdgeFlagv                 = NULL;
	qglEnable                    = NULL;
	qglEnableClientState         = NULL;
	qglEnd                       = NULL;
	qglEndFrame                  = NULL;
	qglEndShadow                 = NULL;
	qglEndList                   = NULL;
	qglEvalCoord1d               = NULL;
	qglEvalCoord1dv              = NULL;
	qglEvalCoord1f               = NULL;
	qglEvalCoord1fv              = NULL;
	qglEvalCoord2d               = NULL;
	qglEvalCoord2dv              = NULL;
	qglEvalCoord2f               = NULL;
	qglEvalCoord2fv              = NULL;
	qglEvalMesh1                 = NULL;
	qglEvalMesh2                 = NULL;
	qglEvalPoint1                = NULL;
	qglEvalPoint2                = NULL;
	qglFeedbackBuffer            = NULL;
	qglFinish                    = NULL;
	qglFlush                     = NULL;
	qglFlushShadow               = NULL;
	qglFogf                      = NULL;
	qglFogfv                     = NULL;
	qglFogi                      = NULL;
	qglFogiv                     = NULL;
	qglFrontFace                 = NULL;
	qglFrustum                   = NULL;
	qglGenLists                  = NULL;
	qglGenTextures               = NULL;
	qglGetBooleanv               = NULL;
	qglGetClipPlane              = NULL;
	qglGetDoublev                = NULL;
	qglGetError                  = NULL;
	qglGetFloatv                 = NULL;
	qglGetIntegerv               = NULL;
	qglGetLightfv                = NULL;
	qglGetLightiv                = NULL;
	qglGetMapdv                  = NULL;
	qglGetMapfv                  = NULL;
	qglGetMapiv                  = NULL;
	qglGetMaterialfv             = NULL;
	qglGetMaterialiv             = NULL;
	qglGetPixelMapfv             = NULL;
	qglGetPixelMapuiv            = NULL;
	qglGetPixelMapusv            = NULL;
	qglGetPointerv               = NULL;
	qglGetPolygonStipple         = NULL;
	qglGetString                 = NULL;
	qglGetTexEnvfv               = NULL;
	qglGetTexEnviv               = NULL;
	qglGetTexGendv               = NULL;
	qglGetTexGenfv               = NULL;
	qglGetTexGeniv               = NULL;
	qglGetTexImage               = NULL;
	qglGetTexLevelParameterfv    = NULL;
	qglGetTexLevelParameteriv    = NULL;
	qglGetTexParameterfv         = NULL;
	qglGetTexParameteriv         = NULL;
	qglHint                      = NULL;
	qglIndexedTriToStrip         = NULL;
	qglIndexMask                 = NULL;
	qglIndexPointer              = NULL;
	qglIndexd                    = NULL;
	qglIndexdv                   = NULL;
	qglIndexf                    = NULL;
	qglIndexfv                   = NULL;
	qglIndexi                    = NULL;
	qglIndexiv                   = NULL;
	qglIndexs                    = NULL;
	qglIndexsv                   = NULL;
	qglIndexub                   = NULL;
	qglIndexubv                  = NULL;
	qglInitNames                 = NULL;
	qglInterleavedArrays         = NULL;
	qglIsEnabled                 = NULL;
	qglIsList                    = NULL;
	qglIsTexture                 = NULL;
	qglLightModelf               = NULL;
	qglLightModelfv              = NULL;
	qglLightModeli               = NULL;
	qglLightModeliv              = NULL;
	qglLightf                    = NULL;
	qglLightfv                   = NULL;
	qglLighti                    = NULL;
	qglLightiv                   = NULL;
	qglLineStipple               = NULL;
	qglLineWidth                 = NULL;
	qglListBase                  = NULL;
	qglLoadIdentity              = NULL;
	qglLoadMatrixd               = NULL;
	qglLoadMatrixf               = NULL;
	qglLoadName                  = NULL;
	qglLogicOp                   = NULL;
	qglMap1d                     = NULL;
	qglMap1f                     = NULL;
	qglMap2d                     = NULL;
	qglMap2f                     = NULL;
	qglMapGrid1d                 = NULL;
	qglMapGrid1f                 = NULL;
	qglMapGrid2d                 = NULL;
	qglMapGrid2f                 = NULL;
	qglMaterialf                 = NULL;
	qglMaterialfv                = NULL;
	qglMateriali                 = NULL;
	qglMaterialiv                = NULL;
	qglMatrixMode                = NULL;
	qglMultMatrixd               = NULL;
	qglMultMatrixf               = NULL;
	qglNewList                   = NULL;
	qglNormal3b                  = NULL;
	qglNormal3bv                 = NULL;
	qglNormal3d                  = NULL;
	qglNormal3dv                 = NULL;
	qglNormal3f                  = NULL;
	qglNormal3fv                 = NULL;
	qglNormal3i                  = NULL;
	qglNormal3iv                 = NULL;
	qglNormal3s                  = NULL;
	qglNormal3sv                 = NULL;
	qglNormalPointer             = NULL;
	qglOrtho                     = NULL;
	qglPassThrough               = NULL;
	qglPixelMapfv                = NULL;
	qglPixelMapuiv               = NULL;
	qglPixelMapusv               = NULL;
	qglPixelStoref               = NULL;
	qglPixelStorei               = NULL;
	qglPixelTransferf            = NULL;
	qglPixelTransferi            = NULL;
	qglPixelZoom                 = NULL;
	qglPointSize                 = NULL;
	qglPolygonMode               = NULL;
	qglPolygonOffset             = NULL;
	qglPolygonStipple            = NULL;
	qglPopAttrib                 = NULL;
	qglPopClientAttrib           = NULL;
	qglPopMatrix                 = NULL;
	qglPopName                   = NULL;
	qglPrioritizeTextures        = NULL;
	qglPushAttrib                = NULL;
	qglPushClientAttrib          = NULL;
	qglPushMatrix                = NULL;
	qglPushName                  = NULL;
	qglRasterPos2d               = NULL;
	qglRasterPos2dv              = NULL;
	qglRasterPos2f               = NULL;
	qglRasterPos2fv              = NULL;
	qglRasterPos2i               = NULL;
	qglRasterPos2iv              = NULL;
	qglRasterPos2s               = NULL;
	qglRasterPos2sv              = NULL;
	qglRasterPos3d               = NULL;
	qglRasterPos3dv              = NULL;
	qglRasterPos3f               = NULL;
	qglRasterPos3fv              = NULL;
	qglRasterPos3i               = NULL;
	qglRasterPos3iv              = NULL;
	qglRasterPos3s               = NULL;
	qglRasterPos3sv              = NULL;
	qglRasterPos4d               = NULL;
	qglRasterPos4dv              = NULL;
	qglRasterPos4f               = NULL;
	qglRasterPos4fv              = NULL;
	qglRasterPos4i               = NULL;
	qglRasterPos4iv              = NULL;
	qglRasterPos4s               = NULL;
	qglRasterPos4sv              = NULL;
	qglReadBuffer                = NULL;
	qglReadPixels                = NULL;
	qglRectd                     = NULL;
	qglRectdv                    = NULL;
	qglRectf                     = NULL;
	qglRectfv                    = NULL;
	qglRecti                     = NULL;
	qglRectiv                    = NULL;
	qglRects                     = NULL;
	qglRectsv                    = NULL;
	qglRenderMode                = NULL;
	qglRotated                   = NULL;
	qglRotatef                   = NULL;
	qglScaled                    = NULL;
	qglScalef                    = NULL;
	qglScissor                   = NULL;
	qglSelectBuffer              = NULL;
	qglShadeModel                = NULL;
	qglStencilFunc               = NULL;
	qglStencilMask               = NULL;
	qglStencilOp                 = NULL;
	qglTexCoord1d                = NULL;
	qglTexCoord1dv               = NULL;
	qglTexCoord1f                = NULL;
	qglTexCoord1fv               = NULL;
	qglTexCoord1i                = NULL;
	qglTexCoord1iv               = NULL;
	qglTexCoord1s                = NULL;
	qglTexCoord1sv               = NULL;
	qglTexCoord2d                = NULL;
	qglTexCoord2dv               = NULL;
	qglTexCoord2f                = NULL;
	qglTexCoord2fv               = NULL;
	qglTexCoord2i                = NULL;
	qglTexCoord2iv               = NULL;
	qglTexCoord2s                = NULL;
	qglTexCoord2sv               = NULL;
	qglTexCoord3d                = NULL;
	qglTexCoord3dv               = NULL;
	qglTexCoord3f                = NULL;
	qglTexCoord3fv               = NULL;
	qglTexCoord3i                = NULL;
	qglTexCoord3iv               = NULL;
	qglTexCoord3s                = NULL;
	qglTexCoord3sv               = NULL;
	qglTexCoord4d                = NULL;
	qglTexCoord4dv               = NULL;
	qglTexCoord4f                = NULL;
	qglTexCoord4fv               = NULL;
	qglTexCoord4i                = NULL;
	qglTexCoord4iv               = NULL;
	qglTexCoord4s                = NULL;
	qglTexCoord4sv               = NULL;
	qglTexCoordPointer           = NULL;
	qglTexEnvf                   = NULL;
	qglTexEnvfv                  = NULL;
	qglTexEnvi                   = NULL;
	qglTexEnviv                  = NULL;
	qglTexGend                   = NULL;
	qglTexGendv                  = NULL;
	qglTexGenf                   = NULL;
	qglTexGenfv                  = NULL;
	qglTexGeni                   = NULL;
	qglTexGeniv                  = NULL;
	qglTexImage1D                = NULL;
	qglTexImage2D                = NULL;
	qglTexImage2DEXT             = NULL;
	qglTexParameterf             = NULL;
	qglTexParameterfv            = NULL;
	qglTexParameteri             = NULL;
	qglTexParameteriv            = NULL;
	qglTexSubImage1D             = NULL;
	qglTexSubImage2D             = NULL;
	qglTranslated                = NULL;
	qglTranslatef                = NULL;
	qglVertex2d                  = NULL;
	qglVertex2dv                 = NULL;
	qglVertex2f                  = NULL;
	qglVertex2fv                 = NULL;
	qglVertex2i                  = NULL;
	qglVertex2iv                 = NULL;
	qglVertex2s                  = NULL;
	qglVertex2sv                 = NULL;
	qglVertex3d                  = NULL;
	qglVertex3dv                 = NULL;
	qglVertex3f                  = NULL;
	qglVertex3fv                 = NULL;
	qglVertex3i                  = NULL;
	qglVertex3iv                 = NULL;
	qglVertex3s                  = NULL;
	qglVertex3sv                 = NULL;
	qglVertex4d                  = NULL;
	qglVertex4dv                 = NULL;
	qglVertex4f                  = NULL;
	qglVertex4fv                 = NULL;
	qglVertex4i                  = NULL;
	qglVertex4iv                 = NULL;
	qglVertex4s                  = NULL;
	qglVertex4sv                 = NULL;
	qglVertexPointer             = NULL;
	qglViewport                  = NULL;

	qglActiveTextureARB          = NULL;
	qglClientActiveTextureARB    = NULL;
	qglMultiTexCoord2fARB        = NULL;
}

/*
** QGL_Init
**
** This is responsible for binding our qgl function pointers to 
** the appropriate GL stuff.  In Windows this means doing a 
** LoadLibrary and a bunch of calls to GetProcAddress.  On other
** operating systems we need to do the right thing, whatever that
** might be.
*/
qboolean QGL_Init( const char *dllname )
{
	qglAccum                     = dllAccum;
	qglAlphaFunc                 = dllAlphaFunc;
	qglAreTexturesResident       = dllAreTexturesResident;
	qglArrayElement              = dllArrayElement;
	qglBegin                     = dllBegin;
	qglBeginEXT                  = dllBeginEXT;
	qglBeginFrame                = dllBeginFrame;
	qglBeginShadow               = dllBeginShadow;
	qglBindTexture               = dllBindTexture;
	qglBitmap                    = dllBitmap;
	qglBlendFunc                 = dllBlendFunc;
	qglCallList                  = dllCallList;
	qglCallLists                 = dllCallLists;
	qglClear                     = dllClear;
	qglClearAccum                = dllClearAccum;
	qglClearColor                = dllClearColor;
	qglClearDepth                = dllClearDepth;
	qglClearIndex                = dllClearIndex;
	qglClearStencil              = dllClearStencil;
	qglClipPlane                 = dllClipPlane;
	qglColor3b                   = dllColor3b;
	qglColor3bv                  = dllColor3bv;
	qglColor3d                   = dllColor3d;
	qglColor3dv                  = dllColor3dv;
	qglColor3f                   = dllColor3f;
	qglColor3fv                  = dllColor3fv;
	qglColor3i                   = dllColor3i;
	qglColor3iv                  = dllColor3iv;
	qglColor3s                   = dllColor3s;
	qglColor3sv                  = dllColor3sv;
	qglColor3ub                  = dllColor3ub;
	qglColor3ubv                 = dllColor3ubv;
	qglColor3ui                  = dllColor3ui;
	qglColor3uiv                 = dllColor3uiv;
	qglColor3us                  = dllColor3us;
	qglColor3usv                 = dllColor3usv;
	qglColor4b                   = dllColor4b;
	qglColor4bv                  = dllColor4bv;
	qglColor4d                   = dllColor4d;
	qglColor4dv                  = dllColor4dv;
	qglColor4f                   = dllColor4f;
	qglColor4fv                  = dllColor4fv;
	qglColor4i                   = dllColor4i;
	qglColor4iv                  = dllColor4iv;
	qglColor4s                   = dllColor4s;
	qglColor4sv                  = dllColor4sv;
	qglColor4ub                  = dllColor4ub;
	qglColor4ubv                 = dllColor4ubv;
	qglColor4ui                  = dllColor4ui;
	qglColor4uiv                 = dllColor4uiv;
	qglColor4us                  = dllColor4us;
	qglColor4usv                 = dllColor4usv;
	qglColorMask                 = dllColorMask;
	qglColorMaterial             = dllColorMaterial;
	qglColorPointer              = dllColorPointer;
	qglCopyPixels                = dllCopyPixels;
	qglCopyTexImage1D            = dllCopyTexImage1D;
	qglCopyTexImage2D            = dllCopyTexImage2D;
	qglCopyTexSubImage1D         = dllCopyTexSubImage1D;
	qglCopyTexSubImage2D         = dllCopyTexSubImage2D;
	qglCullFace                  = dllCullFace;
	qglDeleteLists               = dllDeleteLists;
	qglDeleteTextures            = dllDeleteTextures;
	qglDepthFunc                 = dllDepthFunc;
	qglDepthMask                 = dllDepthMask;
	qglDepthRange                = dllDepthRange;
	qglDisable                   = dllDisable;
	qglDisableClientState        = dllDisableClientState;
	qglDrawArrays                = dllDrawArrays;
	qglDrawBuffer                = dllDrawBuffer;
	qglDrawElements              = dllDrawElements;
	qglDrawPixels                = dllDrawPixels;
	qglEdgeFlag                  = dllEdgeFlag;
	qglEdgeFlagPointer           = dllEdgeFlagPointer;
	qglEdgeFlagv                 = dllEdgeFlagv;
	qglEnable                    = 	dllEnable                   ;
	qglEnableClientState         = 	dllEnableClientState        ;
	qglEnd                       = 	dllEnd                      ;
	qglEndFrame                  = 	dllEndFrame                 ;
	qglEndShadow                 = 	dllEndShadow                ;
	qglEndList                   = 	dllEndList                  ;
	qglEvalCoord1d				 = 	dllEvalCoord1d				;
	qglEvalCoord1dv              = 	dllEvalCoord1dv             ;
	qglEvalCoord1f               = 	dllEvalCoord1f              ;
	qglEvalCoord1fv              = 	dllEvalCoord1fv             ;
	qglEvalCoord2d               = 	dllEvalCoord2d              ;
	qglEvalCoord2dv              = 	dllEvalCoord2dv             ;
	qglEvalCoord2f               = 	dllEvalCoord2f              ;
	qglEvalCoord2fv              = 	dllEvalCoord2fv             ;
	qglEvalMesh1                 = 	dllEvalMesh1                ;
	qglEvalMesh2                 = 	dllEvalMesh2                ;
	qglEvalPoint1                = 	dllEvalPoint1               ;
	qglEvalPoint2                = 	dllEvalPoint2               ;
	qglFeedbackBuffer            = 	dllFeedbackBuffer           ;
	qglFinish                    = 	dllFinish                   ;
	qglFlush                     = 	dllFlush                    ;
	qglFlushShadow               = 	dllFlushShadow              ;
	qglFogf                      = 	dllFogf                     ;
	qglFogfv                     = 	dllFogfv                    ;
	qglFogi                      = 	dllFogi                     ;
	qglFogiv                     = 	dllFogiv                    ;
	qglFrontFace                 = 	dllFrontFace                ;
	qglFrustum                   = 	dllFrustum                  ;
	qglGenLists                  = 	dllGenLists                 ;
	qglGenTextures               = 	dllGenTextures              ;
	qglGetBooleanv               = 	dllGetBooleanv              ;
	qglGetClipPlane              = 	dllGetClipPlane             ;
	qglGetDoublev                = 	dllGetDoublev               ;
	qglGetError                  = 	dllGetError                 ;
	qglGetFloatv                 = 	dllGetFloatv                ;
	qglGetIntegerv               = 	dllGetIntegerv              ;
	qglGetLightfv                = 	dllGetLightfv               ;
	qglGetLightiv                = 	dllGetLightiv               ;
	qglGetMapdv                  = 	dllGetMapdv                 ;
	qglGetMapfv                  = 	dllGetMapfv                 ;
	qglGetMapiv                  = 	dllGetMapiv                 ;
	qglGetMaterialfv             = 	dllGetMaterialfv            ;
	qglGetMaterialiv             = 	dllGetMaterialiv            ;
	qglGetPixelMapfv             = 	dllGetPixelMapfv            ;
	qglGetPixelMapuiv            = 	dllGetPixelMapuiv           ;
	qglGetPixelMapusv            = 	dllGetPixelMapusv           ;
	qglGetPointerv               = 	dllGetPointerv              ;
	qglGetPolygonStipple         = 	dllGetPolygonStipple        ;
	qglGetString                 = 	dllGetString                ;
	qglGetTexEnvfv               = 	dllGetTexEnvfv              ;
	qglGetTexEnviv               = 	dllGetTexEnviv              ;
	qglGetTexGendv               = 	dllGetTexGendv              ;
	qglGetTexGenfv               = 	dllGetTexGenfv              ;
	qglGetTexGeniv               = 	dllGetTexGeniv              ;
	qglGetTexImage               = 	dllGetTexImage              ;
//	qglGetTexLevelParameterfv    = 	dllGetTexLevelParameterfv   ;
//	qglGetTexLevelParameteriv    = 	dllGetTexLevelParameteriv   ;
	qglGetTexParameterfv         = 	dllGetTexParameterfv        ;
	qglGetTexParameteriv         = 	dllGetTexParameteriv        ;
	qglHint                      = 	dllHint                     ;
	qglIndexedTriToStrip         =  dllIndexedTriToStrip        ;
	qglIndexMask                 = 	dllIndexMask                ;
	qglIndexPointer              = 	dllIndexPointer             ;
	qglIndexd                    = 	dllIndexd                   ;
	qglIndexdv                   = 	dllIndexdv                  ;
	qglIndexf                    = 	dllIndexf                   ;
	qglIndexfv                   = 	dllIndexfv                  ;
	qglIndexi                    = 	dllIndexi                   ;
	qglIndexiv                   = 	dllIndexiv                  ;
	qglIndexs                    = 	dllIndexs                   ;
	qglIndexsv                   = 	dllIndexsv                  ;
	qglIndexub                   = 	dllIndexub                  ;
	qglIndexubv                  = 	dllIndexubv                 ;
	qglInitNames                 = 	dllInitNames                ;
	qglInterleavedArrays         = 	dllInterleavedArrays        ;
	qglIsEnabled                 = 	dllIsEnabled                ;
	qglIsList                    = 	dllIsList                   ;
	qglIsTexture                 = 	dllIsTexture                ;
	qglLightModelf               = 	dllLightModelf              ;
	qglLightModelfv              = 	dllLightModelfv             ;
	qglLightModeli               = 	dllLightModeli              ;
	qglLightModeliv              = 	dllLightModeliv             ;
	qglLightf                    = 	dllLightf                   ;
	qglLightfv                   = 	dllLightfv                  ;
	qglLighti                    = 	dllLighti                   ;
	qglLightiv                   = 	dllLightiv                  ;
	qglLineStipple               = 	dllLineStipple              ;
	qglLineWidth                 = 	dllLineWidth                ;
	qglListBase                  = 	dllListBase                 ;
	qglLoadIdentity              = 	dllLoadIdentity             ;
	qglLoadMatrixd               = 	dllLoadMatrixd              ;
	qglLoadMatrixf               = 	dllLoadMatrixf              ;
	qglLoadName                  = 	dllLoadName                 ;
	qglLogicOp                   = 	dllLogicOp                  ;
	qglMap1d                     = 	dllMap1d                    ;
	qglMap1f                     = 	dllMap1f                    ;
	qglMap2d                     = 	dllMap2d                    ;
	qglMap2f                     = 	dllMap2f                    ;
	qglMapGrid1d                 = 	dllMapGrid1d                ;
	qglMapGrid1f                 = 	dllMapGrid1f                ;
	qglMapGrid2d                 = 	dllMapGrid2d                ;
	qglMapGrid2f                 = 	dllMapGrid2f                ;
	qglMaterialf                 = 	dllMaterialf                ;
	qglMaterialfv                = 	dllMaterialfv               ;
	qglMateriali                 = 	dllMateriali                ;
	qglMaterialiv                = 	dllMaterialiv               ;
	qglMatrixMode                = 	dllMatrixMode               ;
	qglMultMatrixd               = 	dllMultMatrixd              ;
	qglMultMatrixf               = 	dllMultMatrixf              ;
	qglNewList                   = 	dllNewList                  ;
	qglNormal3b                  = 	dllNormal3b                 ;
	qglNormal3bv                 = 	dllNormal3bv                ;
	qglNormal3d                  = 	dllNormal3d                 ;
	qglNormal3dv                 = 	dllNormal3dv                ;
	qglNormal3f                  = 	dllNormal3f                 ;
	qglNormal3fv                 = 	dllNormal3fv                ;
	qglNormal3i                  = 	dllNormal3i                 ;
	qglNormal3iv                 = 	dllNormal3iv                ;
	qglNormal3s                  = 	dllNormal3s                 ;
	qglNormal3sv                 = 	dllNormal3sv                ;
	qglNormalPointer             = 	dllNormalPointer            ;
	qglOrtho                     = 	dllOrtho                    ;
	qglPassThrough               = 	dllPassThrough              ;
	qglPixelMapfv                = 	dllPixelMapfv               ;
	qglPixelMapuiv               = 	dllPixelMapuiv              ;
	qglPixelMapusv               = 	dllPixelMapusv              ;
	qglPixelStoref               = 	dllPixelStoref              ;
	qglPixelStorei               = 	dllPixelStorei              ;
	qglPixelTransferf            = 	dllPixelTransferf           ;
	qglPixelTransferi            = 	dllPixelTransferi           ;
	qglPixelZoom                 = 	dllPixelZoom                ;
	qglPointSize                 = 	dllPointSize                ;
	qglPolygonMode               = 	dllPolygonMode              ;
	qglPolygonOffset             = 	dllPolygonOffset            ;
	qglPolygonStipple            = 	dllPolygonStipple           ;
	qglPopAttrib                 = 	dllPopAttrib                ;
	qglPopClientAttrib           = 	dllPopClientAttrib          ;
	qglPopMatrix                 = 	dllPopMatrix                ;
	qglPopName                   = 	dllPopName                  ;
	qglPrioritizeTextures        = 	dllPrioritizeTextures       ;
	qglPushAttrib                = 	dllPushAttrib               ;
	qglPushClientAttrib          = 	dllPushClientAttrib         ;
	qglPushMatrix                = 	dllPushMatrix               ;
	qglPushName                  = 	dllPushName                 ;
	qglRasterPos2d               = 	dllRasterPos2d              ;
	qglRasterPos2dv              = 	dllRasterPos2dv             ;
	qglRasterPos2f               = 	dllRasterPos2f              ;
	qglRasterPos2fv              = 	dllRasterPos2fv             ;
	qglRasterPos2i               = 	dllRasterPos2i              ;
	qglRasterPos2iv              = 	dllRasterPos2iv             ;
	qglRasterPos2s               = 	dllRasterPos2s              ;
	qglRasterPos2sv              = 	dllRasterPos2sv             ;
	qglRasterPos3d               = 	dllRasterPos3d              ;
	qglRasterPos3dv              = 	dllRasterPos3dv             ;
	qglRasterPos3f               = 	dllRasterPos3f              ;
	qglRasterPos3fv              = 	dllRasterPos3fv             ;
	qglRasterPos3i               = 	dllRasterPos3i              ;
	qglRasterPos3iv              = 	dllRasterPos3iv             ;
	qglRasterPos3s               = 	dllRasterPos3s              ;
	qglRasterPos3sv              = 	dllRasterPos3sv             ;
	qglRasterPos4d               = 	dllRasterPos4d              ;
	qglRasterPos4dv              = 	dllRasterPos4dv             ;
	qglRasterPos4f               = 	dllRasterPos4f              ;
	qglRasterPos4fv              = 	dllRasterPos4fv             ;
	qglRasterPos4i               = 	dllRasterPos4i              ;
	qglRasterPos4iv              = 	dllRasterPos4iv             ;
	qglRasterPos4s               = 	dllRasterPos4s              ;
	qglRasterPos4sv              = 	dllRasterPos4sv             ;
	qglReadBuffer                = 	dllReadBuffer               ;
	qglReadPixels                = 	dllReadPixels               ;
	qglCopyBackBufferToTexEXT	 =	dllCopyBackBufferToTexEXT	;
	qglCopyBackBufferToTex		 =	dllCopyBackBufferToTex		;
	qglRectd                     = 	dllRectd                    ;
	qglRectdv                    = 	dllRectdv                   ;
	qglRectf                     = 	dllRectf                    ;
	qglRectfv                    = 	dllRectfv                   ;
	qglRecti                     = 	dllRecti                    ;
	qglRectiv                    = 	dllRectiv                   ;
	qglRects                     = 	dllRects                    ;
	qglRectsv                    = 	dllRectsv                   ;
	qglRenderMode                = 	dllRenderMode               ;
	qglRotated                   = 	dllRotated                  ;
	qglRotatef                   = 	dllRotatef                  ;
	qglScaled                    = 	dllScaled                   ;
	qglScalef                    = 	dllScalef                   ;
	qglScissor                   = 	dllScissor                  ;
	qglSelectBuffer              = 	dllSelectBuffer             ;
	qglShadeModel                = 	dllShadeModel               ;
	qglStencilFunc               = 	dllStencilFunc              ;
	qglStencilMask               = 	dllStencilMask              ;
	qglStencilOp                 = 	dllStencilOp                ;
	qglTexCoord1d                = 	dllTexCoord1d               ;
	qglTexCoord1dv               = 	dllTexCoord1dv              ;
	qglTexCoord1f                = 	dllTexCoord1f               ;
	qglTexCoord1fv               = 	dllTexCoord1fv              ;
	qglTexCoord1i                = 	dllTexCoord1i               ;
	qglTexCoord1iv               = 	dllTexCoord1iv              ;
	qglTexCoord1s                = 	dllTexCoord1s               ;
	qglTexCoord1sv               = 	dllTexCoord1sv              ;
	qglTexCoord2d                = 	dllTexCoord2d               ;
	qglTexCoord2dv               = 	dllTexCoord2dv              ;
	qglTexCoord2f                = 	dllTexCoord2f               ;
	qglTexCoord2fv               = 	dllTexCoord2fv              ;
	qglTexCoord2i                = 	dllTexCoord2i               ;
	qglTexCoord2iv               = 	dllTexCoord2iv              ;
	qglTexCoord2s                = 	dllTexCoord2s               ;
	qglTexCoord2sv               = 	dllTexCoord2sv              ;
	qglTexCoord3d                = 	dllTexCoord3d               ;
	qglTexCoord3dv               = 	dllTexCoord3dv              ;
	qglTexCoord3f                = 	dllTexCoord3f               ;
	qglTexCoord3fv               = 	dllTexCoord3fv              ;
	qglTexCoord3i                = 	dllTexCoord3i               ;
	qglTexCoord3iv               = 	dllTexCoord3iv              ;
	qglTexCoord3s                = 	dllTexCoord3s               ;
	qglTexCoord3sv               = 	dllTexCoord3sv              ;
	qglTexCoord4d                = 	dllTexCoord4d               ;
	qglTexCoord4dv               = 	dllTexCoord4dv              ;
	qglTexCoord4f                = 	dllTexCoord4f               ;
	qglTexCoord4fv               = 	dllTexCoord4fv              ;
	qglTexCoord4i                = 	dllTexCoord4i               ;
	qglTexCoord4iv               = 	dllTexCoord4iv              ;
	qglTexCoord4s                = 	dllTexCoord4s               ;
	qglTexCoord4sv               = 	dllTexCoord4sv              ;
	qglTexCoordPointer           = 	dllTexCoordPointer          ;
	qglTexEnvf                   = 	dllTexEnvf                  ;
	qglTexEnvfv                  = 	dllTexEnvfv                 ;
	qglTexEnvi                   = 	dllTexEnvi                  ;
	qglTexEnviv                  = 	dllTexEnviv                 ;
	qglTexGend                   = 	dllTexGend                  ;
	qglTexGendv                  = 	dllTexGendv                 ;
	qglTexGenf                   = 	dllTexGenf                  ;
	qglTexGenfv                  = 	dllTexGenfv                 ;
	qglTexGeni                   = 	dllTexGeni                  ;
	qglTexGeniv                  = 	dllTexGeniv                 ;
	qglTexImage1D                = 	dllTexImage1D               ;
	qglTexImage2D                = 	dllTexImage2D               ;
	qglTexImage2DEXT             = 	dllTexImage2DEXT            ;
	qglTexParameterf             = 	dllTexParameterf            ;
	qglTexParameterfv            = 	dllTexParameterfv           ;
	qglTexParameteri             = 	dllTexParameteri            ;
	qglTexParameteriv            = 	dllTexParameteriv           ;
	qglTexSubImage1D             = 	dllTexSubImage1D            ;
	qglTexSubImage2D             = 	dllTexSubImage2D            ;
	qglTranslated                = 	dllTranslated               ;
	qglTranslatef                = 	dllTranslatef               ;
	qglVertex2d                  = 	dllVertex2d                 ;
	qglVertex2dv                 = 	dllVertex2dv                ;
	qglVertex2f                  = 	dllVertex2f                 ;
	qglVertex2fv                 = 	dllVertex2fv                ;
	qglVertex2i                  = 	dllVertex2i                 ;
	qglVertex2iv                 = 	dllVertex2iv                ;
	qglVertex2s                  = 	dllVertex2s                 ;
	qglVertex2sv                 = 	dllVertex2sv                ;
	qglVertex3d                  = 	dllVertex3d                 ;
	qglVertex3dv                 = 	dllVertex3dv                ;
	qglVertex3f                  = 	dllVertex3f                 ;
	qglVertex3fv                 = 	dllVertex3fv                ;
	qglVertex3i                  = 	dllVertex3i                 ;
	qglVertex3iv                 = 	dllVertex3iv                ;
	qglVertex3s                  = 	dllVertex3s                 ;
	qglVertex3sv                 = 	dllVertex3sv                ;
	qglVertex4d                  = 	dllVertex4d                 ;
	qglVertex4dv                 = 	dllVertex4dv                ;
	qglVertex4f                  = 	dllVertex4f                 ;
	qglVertex4fv                 = 	dllVertex4fv                ;
	qglVertex4i                  = 	dllVertex4i                 ;
	qglVertex4iv                 = 	dllVertex4iv                ;
	qglVertex4s                  = 	dllVertex4s                 ;
	qglVertex4sv                 = 	dllVertex4sv                ;
	qglVertexPointer             = 	dllVertexPointer            ;
	qglViewport                  = 	dllViewport                 ;

	qglActiveTextureARB          =  dllActiveTextureARB         ;
	qglClientActiveTextureARB    =  dllClientActiveTextureARB   ;
	qglMultiTexCoord2fARB        =  dllMultiTexCoord2fARB       ;

	return qtrue;
}

void QGL_EnableLogging( qboolean enable )
{
}

// Extra functions bound to d3d_ commands for controlling crazy D3D performance things
#if !defined(FINAL_BUILD) && !defined(_XBOX_VC71_MIGRATION)

// D3D_AutoPerfData controls automatic display of performance information:
// framerate, push buffer data, etc... Usage:
// d3d_autoperf				- Toggle on and off
// d3d_autoperf n			- Set display frequency in ms (default 5000)
static void D3D_AutoPerfData_f( void )
{
	static DWORD sdwInterval = 5000;
	static bool sbEnabled = false;

	int numArgs = Cmd_Argc();

	if (numArgs > 2)
	{
		Com_Printf("D3D_AutoPerfData_f: Too many arguments.\n");
	}
	else if (numArgs <= 1)
	{
		sbEnabled = !sbEnabled;
		D3DPERF_SetShowFrameRateInterval(sbEnabled ? sdwInterval : 0);
	}
	else // numArgs == 2 -> Exactly one real argument
	{
		int new_interval = atoi(Cmd_Argv(1));

		if (!new_interval)
		{
			// Fancy way to turn it off, don't change stored interval
			sbEnabled = false;
		}
		else
		{
			// Force it on
			sdwInterval = new_interval;
			sbEnabled = true;
		}
		D3DPERF_SetShowFrameRateInterval(sbEnabled ? sdwInterval : 0);
	}
}

#endif

extern void GLimp_SetGamma(float);


static void _createWindow(int width, int height, int colorbits, qboolean cdsFullscreen)
{
	glConfig.colorBits = colorbits;

	if ( r_depthbits->integer == 0 ) {
		if ( colorbits > 16 ) {
			glConfig.depthBits = 24;
		} else {
			glConfig.depthBits = 16;
		}
	} else {
		glConfig.depthBits = r_depthbits->integer;
	}
	
	glConfig.stencilBits = r_stencilbits->integer;
	if ( glConfig.depthBits < 24 )
	{
		glConfig.stencilBits = 0;
	}
	
	glConfig.displayFrequency = 75;
	glConfig.stereoEnabled = qfalse;

	// VVFIXME : This is surely wrong.
	glConfig.vidHeight = height;
	glConfig.vidWidth = width;

}

enum VideoModes
{
	VM_480i = 0,
	VM_480p,
	VM_720p,
	VM_1080i
};

bool bHadPersistedSurface = false;

void GLW_Init(int width, int height, int colorbits, qboolean cdsFullscreen)
{
	glw_state = new glwstate_t();
#ifdef _XBOX
	g_SPXBPhaseLast = 0x44583330; /* 'DX30' */
#endif
	int mode = VM_480i;

	glw_state->isWidescreen = false;
	if( XGetVideoFlags() & XC_VIDEO_FLAGS_WIDESCREEN )
	{
		glw_state->isWidescreen = true;
		width = 640;

		if( XGetVideoFlags() & XC_VIDEO_FLAGS_HDTV_480p )
		{
			width = 640;
			height = 480;
			mode = VM_480p;
		}

		/*if( XGetVideoFlags() & XC_VIDEO_FLAGS_HDTV_720p )
		{
			width = 1280;
			height = 720;
			mode = VM_720p;
		}

		if( XGetVideoFlags() & XC_VIDEO_FLAGS_HDTV_1080i )
		{
			width = 1920;
			height = 1080;
			mode = VM_1080i;
		}*/
	}

	_createWindow(width, height, colorbits, cdsFullscreen);
	
	glw_state->matrixMode = glwstate_t::MatrixMode_Model;
	glw_state->inDrawBlock = false;
	
	glw_state->serverTU = 0;
	glw_state->clientTU = 0;
	
	glw_state->colorArrayState = false;
	glw_state->vertexArrayState = false;
	glw_state->normalArrayState = false;

	glw_state->cullEnable = true;
	glw_state->cullMode = D3DCULL_CCW;

	glw_state->scissorEnable = false;
	glw_state->scissorBox.x1 = 0;
	glw_state->scissorBox.y1 = 0;
	glw_state->scissorBox.x2 = glConfig.vidWidth;
	glw_state->scissorBox.y2 = glConfig.vidHeight;

	glw_state->shaderMask = 0;

	glw_state->clearColor = D3DCOLOR_RGBA(255, 255, 255, 255);
	glw_state->clearDepth = 1.f;
	glw_state->clearStencil = 0;

	glw_state->currentColor = D3DCOLOR_RGBA(255, 255, 255, 255);

	glw_state->viewport.MinZ = 0.f;
	glw_state->viewport.MaxZ = 1.f;

	for (int t = 0; t < GLW_MAX_TEXTURE_STAGES; ++t)
	{
		glw_state->textureEnv[t] = D3DTOP_MODULATE;
		glw_state->texCoordArrayState[t] = false;
		glw_state->currentTexture[t] = 0;
		glw_state->textureStageDirty[t] = false;
	}

	glw_state->textureBindNum = 1;
	
	D3DPRESENT_PARAMETERS present;
	fillPresentParameters(&present, width, height, true);

#ifdef _XBOX
	g_SPXBPhaseLast = 0x44583331; /* 'DX31' */
	XBL("GLW_Init: Direct3DCreate8\n");
#endif
	glw_state->d3d = Direct3DCreate8(D3D_SDK_VERSION);
	if (!glw_state->d3d)
	{
#ifdef _XBOX
		g_SPXBPhaseLast = 0x44584631; /* 'DXF1' */
		XBL("GLW_Init: Direct3DCreate8 FAILED\n");
#endif
		Com_Printf("Failed to create Direct3D8 interface.\n");
		return;
	}

#ifdef _XBOX
	g_SPXBPhaseLast = 0x44583332; /* 'DX32' */
	XBL("GLW_Init: SetPushBufferSize 1024K/32K\n");
#endif
	HRESULT hrPB = glw_state->d3d->SetPushBufferSize(1024 * 1024, 32 * 1024);
	if (FAILED(hrPB))
	{
#ifdef _XBOX
		g_SPXBPhaseLast = 0x44584632; /* 'DXF2' */
		XBLF("GLW_Init: SetPushBufferSize FAILED hr=0x%08x\n", (unsigned)hrPB);
#endif
		Com_Printf("Failed to configure Direct3D8 push buffer.\n");
		return;
	}

#ifdef _XBOX
	g_SPXBPhaseLast = 0x44583333; /* 'DX33' */
	XBLF("GLW_Init: CreateDevice %dx%d fmt=%d depth=0x%x flags=0x%x\n",
		width, height, (int)present.BackBufferFormat,
		(int)present.AutoDepthStencilFormat, (unsigned)present.Flags);
#endif

	HRESULT hrCD = glw_state->d3d->CreateDevice(D3DADAPTER_DEFAULT,
								D3DDEVTYPE_HAL,
								NULL,
								D3DCREATE_HARDWARE_VERTEXPROCESSING,
								&present,
								&glw_state->device);
	if (hrCD != D3D_OK)
	{
#ifdef _XBOX
		g_SPXBPhaseLast = 0x44584633; /* 'DXF3' */
		XBLF("GLW_Init: CreateDevice FAILED hr=0x%08x device=%p\n",
			(unsigned)hrCD, (void*)glw_state->device);
#endif
		Com_Printf("Failed to create device. That's bad.\n");
		/* device pointer is NULL — everything below requires it; bail out */
		return;
	}

#ifdef _XBOX
	g_SPXBPhaseLast = 0x44583334; /* 'DX34' */
	XBL("GLW_Init: CreateDevice OK\n");

	// Reserve the persistent submission buffer before map textures fragment
	// contiguous graphics memory. Indexed storage remains lazy because the
	// direct-push path does not require it.
	if (!EnsureNativeVertexRing())
	{
		Com_Error(ERR_FATAL, "Native D3D8 vertex ring allocation failed");
		return;
	}

	// Registered Xbox textures must live in contiguous memory. Initialize
	// this only after CreateDevice; doing it during static/zone startup is
	// unsafe on retail hardware.
	g_SPXBPhaseLast = 0x44585430; /* 'DXT0' */
	// Leave enough general-purpose memory for EF's parsed character models.
	// Borg maps exhaust both the process heap and fragmented zone when this
	// reservation consumes 19 MiB before model registration begins.
	gTextures.Initialize(15 * 1024 * 1024);
	if (!gTextures.Capacity())
	{
		g_SPXBPhaseLast = 0x44585446; /* 'DXTF' */
		Com_Error(ERR_FATAL, "Native D3D8 texture pool allocation failed");
		return;
	}
	g_SPXBPhaseLast = 0x44585431; /* 'DXT1' */
	XBLF("GLW_Init: native texture pool capacity=%u\n",
		(unsigned)gTextures.Capacity());
#endif

//	qglEnable(GL_VSYNC);

	/* Guard: device must be non-NULL before vtable calls */
	if (!glw_state->device)
	{
#ifdef _XBOX
		XBL("GLW_Init: device NULL after CreateDevice — aborting\n");
#endif
		return;
	}

	// Immediately check to see if there's 1.2MB being wasted on a persisted surface:
	IDirect3DSurface8 *pPersistedSurf;
	glw_state->device->GetPersistedSurface( &pPersistedSurf );
	bHadPersistedSurface = (pPersistedSurf != NULL);

	for (int m = 0; m < glwstate_t::Num_MatrixModes; ++m)
	{
		D3DXCreateMatrixStack(0, &glw_state->matrixStack[m]);
		glw_state->matrixStack[m]->LoadIdentity();
		glw_state->matricesDirty[m] = false;
	}

	// VVFIXME: Hack - turn off lighting
	dllDisable(GL_LIGHTING);

	// Set a material (for lighting)
	memset( &glw_state->mtrl, 0, sizeof(D3DMATERIAL8) );
	glw_state->mtrl.Diffuse.r = glw_state->mtrl.Ambient.r = 1.0f;
	glw_state->mtrl.Diffuse.g = glw_state->mtrl.Ambient.g = 1.0f;
	glw_state->mtrl.Diffuse.b = glw_state->mtrl.Ambient.b = 1.0f;
	glw_state->mtrl.Diffuse.a = glw_state->mtrl.Ambient.a = 1.0f;
	glw_state->device->SetMaterial( &glw_state->mtrl );
#ifdef _XBOX
	g_SPXBPhaseLast = 0x44583335; /* 'DX35' */
#endif
	// Gamma hack
	GLimp_SetGamma(1.3f);

	// Set up our directional light (used for diffuse lighting)
	memset(&glw_state->dirLight, 0, sizeof(D3DLIGHT8));

	// Set up a white point light.
	glw_state->dirLight.Type = D3DLIGHT_DIRECTIONAL;
	glw_state->dirLight.Diffuse.r  = 1.0f;
	glw_state->dirLight.Diffuse.g  = 1.0f;
	glw_state->dirLight.Diffuse.b  = 1.0f;
	glw_state->dirLight.Direction.x = 1.0f;
	glw_state->dirLight.Direction.y = 0.0f;
	glw_state->dirLight.Direction.z = 0.0f;

	// Don't attenuate.
	glw_state->dirLight.Attenuation0 = 1.0f; 
	glw_state->dirLight.Range        = 1000.0f;

	//glw_state->drawArray = new DWORD[SHADER_MAX_VERTEXES * 12];
	glw_state->drawArray = NULL;

#ifdef _XBOX
#ifdef VV_LIGHTING
//	glw_state->flareEffect = new FlareEffect;
//	glw_state->flareEffect->Initialize();
	glw_state->lightEffects = new LightEffects;
	StencilShadower.Initialize();
#endif // VV_LIGHTING
//	HDREffect.Initialize();
#endif

#if !defined(FINAL_BUILD) && !defined(_XBOX_VC71_MIGRATION)
	Cmd_AddCommand("d3d_autoperf", D3D_AutoPerfData_f);
#endif

#if YELLOW_MODE
	initYellowMode();
#endif // YELLOW_MODE
}

void GLW_Shutdown(void)
{
	if (!glw_state)
	{
		return;
	}
#ifdef _XBOX
#ifdef VV_LIGHTING
	delete glw_state->lightEffects;
#endif
#endif

	for (int m = 0; m < glwstate_t::Num_MatrixModes; ++m)
	{
		if (glw_state->matrixStack[m])
		{
			glw_state->matrixStack[m]->Release();
		}
	}

#ifdef _XBOX
	ReleaseNativeDrawRing();
#endif

	if (glw_state->device)
	{
		glw_state->device->Release();
	}
	if (glw_state->d3d)
	{
		glw_state->d3d->Release();
	}

#ifdef _XBOX
//	delete glw_state->flareEffect;
#endif

	delete glw_state;
	glw_state = NULL;
}

//-----------------------------------------------------------------------------
// Compressed Screen Shot code for the save game system
//-----------------------------------------------------------------------------

//#define DISABLE_SCREENSHOT
#define CSS_IMAGE_HDR_SIZE			2048									
#define	CSS_IMAGE_DATA_SIZE			((SAVE_GAME_IMAGE_W * SAVE_GAME_IMAGE_H) / 2 )	

struct XprImageHeader
{
    XPR_HEADER        xpr;           // Standard XPR struct
    IDirect3DTexture8 txt;           // Standard D3D texture struct
    DWORD             dwEndOfHeader; // 0xFFFFFFFF
};

struct XprImage
{
    XprImageHeader hdr;
    CHAR           strPad[ CSS_IMAGE_HDR_SIZE - sizeof( XprImageHeader ) ];
    BYTE           pBits[ CSS_IMAGE_DATA_SIZE ];     // data bits
};

//-----------------------------------------------------------------------------
// SaveCompressedScreenshot
// Saves two screenshots - one is full-screen at 256x128, and placed in
// Z:\screenshot.xbx. The other is cropped, and 64x64 for the dashboard,
// and placed in  Z:\saveimage.xbx
//-----------------------------------------------------------------------------
void SaveCompressedScreenshot( void )
{
#ifndef DISABLE_SCREENSHOT
	LPDIRECT3DSURFACE8 screenShot = 0;
	HRESULT res;
	glw_state->device->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &screenShot);

	// Copy over the screen shot to the new image that is CSS_IMAGE_WH x CSS_IMAGE_WH
	LPDIRECT3DSURFACE8 compressedSaveGameImage = 0;
	// New: we only screenshot the title-safe area, to make the image clearer in the ui:
	RECT srcRect;
	srcRect.left = 48;
	srcRect.top = 36;
	srcRect.right = 592;
	srcRect.bottom = 444;
    glw_state->device->CreateImageSurface( SAVE_GAME_IMAGE_W, SAVE_GAME_IMAGE_H, D3DFMT_DXT1, &compressedSaveGameImage );
	D3DXLoadSurfaceFromSurface( compressedSaveGameImage, NULL, NULL, screenShot, NULL, &srcRect, D3DX_DEFAULT, D3DCOLOR( 0 ) );
  
	// Write out the large screenshot (our 256x128 for display in the UI)
	res = XGWriteSurfaceOrTextureToXPR( compressedSaveGameImage, "Z:\\screenshot.xbx", TRUE );

	// Free the compressed 256x128 image
    if ( compressedSaveGameImage )
		compressedSaveGameImage->Release();

	// Re-load the file and build a signature. File should always be less than 32k
	// Then append to the file. This is a poor example of how-not-to-write-code:
	byte xbxFile[32*1024];
	DWORD dwSize;

	// Open and read:
	HANDLE hFile = CreateFile( "Z:\\screenshot.xbx", GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, 0, 0 );
	ReadFile( hFile, xbxFile, sizeof(xbxFile), &dwSize, NULL );

	// Build a signature:
	XCALCSIG_SIGNATURE xbxSig;
	HANDLE hSig = XCalculateSignatureBegin( XCALCSIG_FLAG_SAVE_GAME );
	XCalculateSignatureUpdate( hSig, xbxFile, dwSize );
	XCalculateSignatureEnd( hSig, &xbxSig );

	// Append:
	SetFilePointer( hFile, 0, NULL, FILE_END );
	WriteFile( hFile, &xbxSig, sizeof(xbxSig), &dwSize, NULL );
	CloseHandle( hFile );

	// Make another surface, this one only 64x64 for the dashboard:
	glw_state->device->CreateImageSurface( 64, 64, D3DFMT_DXT1, &compressedSaveGameImage );

	// We take a 240x240 region from the center of the screen, offset if in third person:
	srcRect;
	srcRect.left = (glConfig.vidWidth / 2) - 120;
	srcRect.right = srcRect.left + 240;

	srcRect.top = (glConfig.vidHeight / 2) - 120;
	if( Cvar_VariableValue( "cg_thirdPerson" ) )
		srcRect.top += 60;
	srcRect.bottom = srcRect.top + 240;

	D3DXLoadSurfaceFromSurface( compressedSaveGameImage, NULL, NULL, screenShot, NULL, &srcRect, D3DX_DEFAULT, D3DCOLOR( 0 ) );

	// Write out the small screenshot (64x64)
	res = XGWriteSurfaceOrTextureToXPR( compressedSaveGameImage, "Z:\\saveimage.xbx", TRUE );

	// Free the compressed 64x64 image
    if ( compressedSaveGameImage )
		compressedSaveGameImage->Release();

	// Free the big screenshot 640x480x4?
	if ( screenShot )
		screenShot->Release();

#endif
}

//-----------------------------------------------------------------------------
// LoadCompressedScreenshot
// Loads a .xbx screenshot file and replaces the current texture
//-----------------------------------------------------------------------------
BOOL LoadCompressedScreenshot(const char* filename)
{
#ifndef DISABLE_SCREENSHOT
	// get the current texture
	glwstate_t::TextureInfo* info = _getCurrentTexture(glw_state->serverTU);
	if (info == NULL) return FALSE;

	// locals
	DWORD				dwBytesRead;
	BOOL				bSuccess;

	// See if the image file for this saved game exists
	HANDLE hFile = CreateFile( filename, GENERIC_READ, 0, NULL, OPEN_EXISTING,0, NULL );

	if( hFile == INVALID_HANDLE_VALUE )
	{
		// Extreme failure case. Skip other work below, but still blank out the screenshot
		// ONE: Take all textures out of stages, in case ours is locked:
		glw_state->device->SetTexture( 0, NULL );
		glw_state->device->SetTexture( 1, NULL );
		glw_state->textureStageDirty[0] = true;
		glw_state->textureStageDirty[1] = true;

		// TWO: Now wait to be sure that the texture we're about to replace isn't locked by GPU:
		while( info->mipmap->IsBusy() )
		{
			// Do nothing
		}

		// THREE: Use LockRect to get a pointer to the texture's data:
		D3DLOCKED_RECT lr;
		info->mipmap->LockRect( 0, &lr, NULL, D3DLOCK_TILED );

		// FOUR: Copy the texture data from the file:
		memset( lr.pBits, 0, CSS_IMAGE_DATA_SIZE );		
		info->mipmap->UnlockRect( 0 );

		return false;
	}

	// Non-signature portion of the file, which we read all at once:
	XprImage image;

	// Read everything but the signature from disk:
	bSuccess = ReadFile( hFile, &image, sizeof( XprImage ), &dwBytesRead, NULL );

	// Validate that data:
	bSuccess &=	dwBytesRead					== sizeof( XprImage ) &&
				image.hdr.xpr.dwMagic		== XPR_MAGIC_VALUE &&
				image.hdr.xpr.dwTotalSize	== CSS_IMAGE_HDR_SIZE + CSS_IMAGE_DATA_SIZE &&
				image.hdr.xpr.dwHeaderSize	== CSS_IMAGE_HDR_SIZE &&
				image.hdr.dwEndOfHeader		== 0xFFFFFFFF;

	// Now read the signature:
	XCALCSIG_SIGNATURE xbxSig;
	bSuccess &= ReadFile( hFile, &xbxSig, sizeof( xbxSig ), &dwBytesRead, NULL );

	// Verify that we're at the end of the file (it's properly sized)
	if( SetFilePointer( hFile, 0, NULL, FILE_END ) != (sizeof( XprImage ) + sizeof( xbxSig )) )
		bSuccess = false;
	CloseHandle( hFile );

	// Re-sign the data:
	XCALCSIG_SIGNATURE datSig;
	HANDLE hSig = XCalculateSignatureBegin( XCALCSIG_FLAG_SAVE_GAME );
	XCalculateSignatureUpdate( hSig, (const BYTE *) &image, sizeof( image ) );
	XCalculateSignatureEnd( hSig, &datSig );

	// Compare the signatures:
	if( memcmp( &xbxSig, &datSig, sizeof( xbxSig ) ) != 0 )
		bSuccess = false;

	// Now we update the texture. We do this even if the file was bad, using black instead:

	// ONE: Take all textures out of stages, in case ours is locked:
	glw_state->device->SetTexture( 0, NULL );
	glw_state->device->SetTexture( 1, NULL );
	glw_state->textureStageDirty[0] = true;
	glw_state->textureStageDirty[1] = true;

	// TWO: Now wait to be sure that the texture we're about to replace isn't locked by GPU:
	while( info->mipmap->IsBusy() )
	{
		// Do nothing
	}

	// THREE: Use LockRect to get a pointer to the texture's data:
	D3DLOCKED_RECT lr;
	info->mipmap->LockRect( 0, &lr, NULL, D3DLOCK_TILED );

	if( bSuccess )
	{
		// FOUR: Copy the texture data from the file:
		memcpy( lr.pBits, image.pBits, sizeof( image.pBits ) );
	}
	else
	{
		memset( lr.pBits, 0, CSS_IMAGE_DATA_SIZE );
	}
	
	info->mipmap->UnlockRect( 0 );

	return bSuccess;
#else
	return FALSE;
#endif
}

bool CreateVertexShader( const CHAR* strFilename, const DWORD* pdwVertexDecl, DWORD* pdwVertexShader )
{
    HRESULT hr;

    // Open the vertex shader file
    HANDLE hFile;
    DWORD dwNumBytesRead;
    hFile = CreateFile( strFilename, GENERIC_READ, FILE_SHARE_READ, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_READONLY, NULL );
    if( hFile == INVALID_HANDLE_VALUE )
        return false;

    // Allocate memory to read the vertex shader file
    DWORD dwSize = GetFileSize(hFile, NULL);
    BYTE* pData  = new BYTE[dwSize+4];
    if( NULL == pData )
    {
        CloseHandle( hFile );
        return false;
    }
    ZeroMemory( pData, dwSize+4 );

    // Read the pre-compiled vertex shader microcode
    ReadFile(hFile, pData, dwSize, &dwNumBytesRead, 0);
    
    // Create the vertex shader
    hr = glw_state->device->CreateVertexShader( pdwVertexDecl, (const DWORD*)pData,
                                        pdwVertexShader, 0 );

    // Cleanup and return
    CloseHandle( hFile );
    delete [] pData;

	if(hr == S_OK)
		return true;

	return false;
}


bool CreatePixelShader( const CHAR* strFilename, DWORD* pdwPixelShader )
{
    HRESULT hr;

    // Open the pixel shader file
    HANDLE hFile;
    DWORD dwNumBytesRead;
    hFile = CreateFile( strFilename, GENERIC_READ, FILE_SHARE_READ, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_READONLY, NULL );
    if( hFile == INVALID_HANDLE_VALUE )
        return false;

    // Load the pre-compiled pixel shader microcode directly.
    D3DPIXELSHADERDEF psdf;
    ZeroMemory( &psdf, sizeof( psdf ) );
    ReadFile( hFile, &psdf, sizeof( psdf ), &dwNumBytesRead, NULL );

    // Create the pixel shader
    if( dwNumBytesRead != sizeof( psdf ) ||
        FAILED( hr = glw_state->device->CreatePixelShader( &psdf, pdwPixelShader ) ) )
    {
        CloseHandle( hFile );
        return false;
    }

    // Cleanup
    CloseHandle( hFile );

    return true;
}
