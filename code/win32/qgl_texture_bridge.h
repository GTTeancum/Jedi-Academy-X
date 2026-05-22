/*
 * qgl_texture_bridge.h
 *
 * Bridges JKA's GL texture API expectations to FakeGL's OpenGL 1.x core
 * subset.  JKA passes its own DDS-family format extensions (GL_DDS1_EXT,
 * GL_DDS5_EXT, GL_DDS_RGB16_EXT, GL_DDS_RGBA32_EXT) plus standard
 * GL_COMPRESSED_*_S3TC_DXT[1/3/5]_EXT compressed textures to qglTexImage2D
 * and qglTexImage2DEXT.  FakeGL's glTexImage2D only understands core GL
 * formats (GL_RGBA, GL_RGB, GL_LUMINANCE, GL_ALPHA, etc.).
 *
 * This bridge:
 *   - Allocates real, sequential texture IDs (qglGenTextures was a no-op
 *     returning uninitialized stack memory)
 *   - Detects DDS file containers (with "DDS " header) vs raw compressed
 *     blocks
 *   - Decompresses DXT1/DXT3/DXT5 to RGBA8888 in CPU memory using the
 *     reference algorithms from the S3 Texture Compression specification
 *   - Strips DDS headers for uncompressed DDS variants
 *   - Forwards the final RGBA buffer to FakeGL's glTexImage2D as GL_RGBA
 *
 * The bridge does NOT manage GPU resources directly — FakeGL owns those.
 * The bridge only converts JKA-shaped data into FakeGL-acceptable shape.
 *
 * Bound to qgl_* function pointers in win_qgl_dx8.cpp's QGL_Init.
 */

#ifndef _QGL_TEXTURE_BRIDGE_H_
#define _QGL_TEXTURE_BRIDGE_H_

#include "../renderer/qgl_console.h"  /* GLenum, GLuint, etc. */

#ifdef __cplusplus
extern "C" {
#endif

void APIENTRY QGLBridge_GenTextures(GLsizei n, GLuint *textures);
void APIENTRY QGLBridge_DeleteTextures(GLsizei n, const GLuint *textures);
void APIENTRY QGLBridge_BindTexture(GLenum target, GLuint texture);
void APIENTRY QGLBridge_TexImage2D(GLenum target, GLint level, GLint internalformat,
                                   GLsizei width, GLsizei height, GLint border,
                                   GLenum format, GLenum type, const GLvoid *pixels);
void APIENTRY QGLBridge_TexImage2DEXT(GLenum target, GLint level, GLint numlevels,
                                      GLint internalformat,
                                      GLsizei width, GLsizei height, GLint border,
                                      GLenum format, GLenum type, const GLvoid *pixels);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _QGL_TEXTURE_BRIDGE_H_ */
