/*
 * qgl_texture_bridge.cpp
 *
 * Implementation of the JKA-GL → FakeGL texture format bridge.
 * See qgl_texture_bridge.h for design rationale.
 */

/* Required by qgl_console.h — APIENTRY/__stdcall match for qgl_* signatures.
 * Must be defined before any header chain pulls qgl_console.h. */
#define APIENTRY __stdcall
#define WINAPI   __stdcall

#include "../server/exe_headers.h"  /* PCH */
#include "qgl_texture_bridge.h"

#include "../renderer/qgl_console.h"   /* GL enums + qgl_* type decls */
#include "../renderer/glext_console.h" /* GL_DDS1_EXT etc. */
#include "xb_log.h"

#include <stdlib.h>  /* malloc / free */
#include <string.h>  /* memcpy / memset */
#include <new>

/* FakeGL exports the gl_* implementations we ultimately route to.  These
 * symbols are __stdcall (via APIENTRY) per gl_fakegl.cpp's WINAPI signatures.
 * Forward-declared here so the bridge can call them. */
extern "C" {
    void APIENTRY glTexImage2D(GLenum target, GLint level, GLint internalformat,
                                GLsizei width, GLsizei height, GLint border,
                                GLenum format, GLenum type, const GLvoid *pixels);
}

/* FakeGL's "extension" texture-bind function — bound by wglGetProcAddress
 * in QGL_Init, but we need a direct callable for forwarding.  FakeGL has
 * no exported C-callable glBindTexture (only static APIENTRY behind the
 * registry).  We bridge via a function pointer cached from wglGetProcAddress. */
typedef void (APIENTRY *PFN_GLBINDTEXTUREEXT)(GLenum target, GLuint texture);
static PFN_GLBINDTEXTUREEXT s_glBindTextureExt = NULL;

extern "C" PROC WINAPI wglGetProcAddress(LPCSTR);

static void EnsureBindTextureExt(void)
{
    if (!s_glBindTextureExt) {
        s_glBindTextureExt = (PFN_GLBINDTEXTUREEXT)wglGetProcAddress("glBindTextureEXT");
    }
}

/* =========================================================================
 *  Sequential texture-ID generator.
 *  ---
 *  qglGenTextures was a no-op in our previous adapter, leaving the caller's
 *  GLuint with uninitialized stack memory.  FakeGL's texture map (textureXlat)
 *  keys on whatever ID we hand it, so we just need unique non-zero IDs.
 *  IDs are never recycled — JKA never approaches the 4-billion limit and the
 *  simplicity is worth it.
 * ========================================================================= */
static GLuint s_nextTextureId = 1;

void APIENTRY QGLBridge_GenTextures(GLsizei n, GLuint *textures)
{
    if (!textures || n <= 0) return;
    for (GLsizei i = 0; i < n; ++i) {
        textures[i] = s_nextTextureId++;
        if (s_nextTextureId == 0) s_nextTextureId = 1;  /* wraparound guard */
    }
}

void APIENTRY QGLBridge_DeleteTextures(GLsizei /*n*/, const GLuint * /*textures*/)
{
    /* FakeGL has no C-callable delete entry point.  Textures it owns are
     * freed when the device is reset or destroyed.  Leaking IDs is harmless
     * (we don't recycle) and matches the previous no-op behaviour. */
}

void APIENTRY QGLBridge_BindTexture(GLenum target, GLuint texture)
{
    EnsureBindTextureExt();
    if (s_glBindTextureExt) {
        s_glBindTextureExt(target, texture);
    }
}

/* DDS_HEADER and DDS_PIXELFORMAT are defined in code/renderer/tr_local.h
 * (which we get via exe_headers.h above).  Both 124-byte layouts are
 * identical to the Microsoft DDS spec; we use the existing definitions
 * to avoid C2011 redefinition errors. */

/* "DDS " magic (FourCC encoded little-endian: 0x20534444) */
#define DDS_MAGIC 0x20534444u

/* DXT FourCC codes, little-endian DWORDs */
#define FOURCC_DXT1 0x31545844u  /* 'D','X','T','1' */
#define FOURCC_DXT3 0x33545844u
#define FOURCC_DXT5 0x35545844u

/* =========================================================================
 *  DXT1 / DXT3 / DXT5 decompression
 *  ---
 *  Reference implementations per the S3 Texture Compression specification
 *  (https://registry.khronos.org/OpenGL/extensions/EXT/EXT_texture_compression_s3tc.txt).
 *  4×4 pixel blocks.  Output is RGBA8888 (32 bits per pixel) in standard
 *  row-major order with the same row stride as the source's logical layout.
 * ========================================================================= */

/* Unpack a single RGB565 colour into 32-bit ARGB (alpha set to 0xFF). */
static inline DWORD UnpackRGB565(WORD c)
{
    const DWORD r5 = (c >> 11) & 0x1F;
    const DWORD g6 = (c >>  5) & 0x3F;
    const DWORD b5 = (c      ) & 0x1F;
    /* Bit-replicate to fill 8 bits — the canonical RGB565→RGB888 expansion.
     * For 5-bit channels: out = (v << 3) | (v >> 2)
     * For 6-bit channels: out = (v << 2) | (v >> 4) */
    const DWORD r = (r5 << 3) | (r5 >> 2);
    const DWORD g = (g6 << 2) | (g6 >> 4);
    const DWORD b = (b5 << 3) | (b5 >> 2);
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

/* Decode a single 8-byte DXT1 colour block into 16 RGBA8888 pixels.
 * `dst` is a pointer to the upper-left of the 4×4 destination block.
 * `dstStride` is the destination row stride in DWORDs (= image width). */
static void DecodeDXT1Block(const BYTE *src, DWORD *dst, int dstStride)
{
    const WORD c0w = (WORD)(src[0] | (src[1] << 8));
    const WORD c1w = (WORD)(src[2] | (src[3] << 8));
    const DWORD indices = (DWORD)src[4] | ((DWORD)src[5] << 8)
                        | ((DWORD)src[6] << 16) | ((DWORD)src[7] << 24);

    DWORD palette[4];
    palette[0] = UnpackRGB565(c0w);
    palette[1] = UnpackRGB565(c1w);

    if (c0w > c1w) {
        /* 4-colour mode: 2 interpolated opaque colours */
        const DWORD p0 = palette[0], p1 = palette[1];
        DWORD r0 = (p0 >> 16) & 0xFF, g0 = (p0 >> 8) & 0xFF, b0 = p0 & 0xFF;
        DWORD r1 = (p1 >> 16) & 0xFF, g1 = (p1 >> 8) & 0xFF, b1 = p1 & 0xFF;
        palette[2] = 0xFF000000u
                   | (((2*r0 + r1) / 3) << 16)
                   | (((2*g0 + g1) / 3) <<  8)
                   |  ((2*b0 + b1) / 3);
        palette[3] = 0xFF000000u
                   | (((r0 + 2*r1) / 3) << 16)
                   | (((g0 + 2*g1) / 3) <<  8)
                   |  ((b0 + 2*b1) / 3);
    } else {
        /* 3-colour mode: 1 interpolated colour + 1 transparent black */
        const DWORD p0 = palette[0], p1 = palette[1];
        DWORD r0 = (p0 >> 16) & 0xFF, g0 = (p0 >> 8) & 0xFF, b0 = p0 & 0xFF;
        DWORD r1 = (p1 >> 16) & 0xFF, g1 = (p1 >> 8) & 0xFF, b1 = p1 & 0xFF;
        palette[2] = 0xFF000000u
                   | (((r0 + r1) / 2) << 16)
                   | (((g0 + g1) / 2) <<  8)
                   |  ((b0 + b1) / 2);
        palette[3] = 0x00000000u;  /* fully transparent black */
    }

    for (int y = 0; y < 4; ++y) {
        DWORD *row = dst + y * dstStride;
        for (int x = 0; x < 4; ++x) {
            const int idx = (indices >> (2 * (4 * y + x))) & 0x3;
            row[x] = palette[idx];
        }
    }
}

/* Decode a single 8-byte DXT5 alpha block.  Writes 16 alpha bytes into
 * dst[0..15] in scanline order (4 rows × 4 cols). */
static void DecodeDXT5AlphaBlock(const BYTE *src, BYTE *dst16)
{
    const BYTE a0 = src[0];
    const BYTE a1 = src[1];

    BYTE alpha[8];
    alpha[0] = a0;
    alpha[1] = a1;

    if (a0 > a1) {
        /* 8-alpha mode: 6 interpolated */
        for (int i = 1; i <= 6; ++i) {
            alpha[i + 1] = (BYTE)(((7 - i) * a0 + i * a1) / 7);
        }
    } else {
        /* 6-alpha mode: 4 interpolated + 0 + 255 */
        for (int i = 1; i <= 4; ++i) {
            alpha[i + 1] = (BYTE)(((5 - i) * a0 + i * a1) / 5);
        }
        alpha[6] = 0;
        alpha[7] = 255;
    }

    /* 48 bits of 3-bit indices packed across src[2..7] (little-endian). */
    DWORD lo = (DWORD)src[2] | ((DWORD)src[3] << 8) | ((DWORD)src[4] << 16);
    DWORD hi = (DWORD)src[5] | ((DWORD)src[6] << 8) | ((DWORD)src[7] << 16);

    for (int i = 0; i < 8; ++i) {
        dst16[i]     = alpha[(lo >> (3 * i)) & 0x7];
        dst16[i + 8] = alpha[(hi >> (3 * i)) & 0x7];
    }
}

/* Decode a single 16-byte DXT5 block into 16 RGBA8888 pixels. */
static void DecodeDXT5Block(const BYTE *src, DWORD *dst, int dstStride)
{
    BYTE alpha[16];
    DecodeDXT5AlphaBlock(src, alpha);

    /* DXT5's colour block always uses the 4-colour mode (no transparent
     * colour entry); decode it like DXT1 4-colour, then override alpha. */
    const WORD c0w = (WORD)(src[8] | (src[9] << 8));
    const WORD c1w = (WORD)(src[10] | (src[11] << 8));
    const DWORD indices = (DWORD)src[12] | ((DWORD)src[13] << 8)
                        | ((DWORD)src[14] << 16) | ((DWORD)src[15] << 24);

    DWORD palette[4];
    palette[0] = UnpackRGB565(c0w);
    palette[1] = UnpackRGB565(c1w);
    {
        const DWORD p0 = palette[0], p1 = palette[1];
        DWORD r0 = (p0 >> 16) & 0xFF, g0 = (p0 >> 8) & 0xFF, b0 = p0 & 0xFF;
        DWORD r1 = (p1 >> 16) & 0xFF, g1 = (p1 >> 8) & 0xFF, b1 = p1 & 0xFF;
        palette[2] = 0xFF000000u
                   | (((2*r0 + r1) / 3) << 16)
                   | (((2*g0 + g1) / 3) <<  8)
                   |  ((2*b0 + b1) / 3);
        palette[3] = 0xFF000000u
                   | (((r0 + 2*r1) / 3) << 16)
                   | (((g0 + 2*g1) / 3) <<  8)
                   |  ((b0 + 2*b1) / 3);
    }

    for (int y = 0; y < 4; ++y) {
        DWORD *row = dst + y * dstStride;
        for (int x = 0; x < 4; ++x) {
            const int linIdx = 4 * y + x;
            const int colIdx = (indices >> (2 * linIdx)) & 0x3;
            const DWORD rgb = palette[colIdx] & 0x00FFFFFFu;
            row[x] = ((DWORD)alpha[linIdx] << 24) | rgb;
        }
    }
}

/* Decode a single 16-byte DXT3 block into 16 RGBA8888 pixels.  Like DXT5
 * but the alpha block is 16 explicit 4-bit alpha values instead of
 * interpolated. */
static void DecodeDXT3Block(const BYTE *src, DWORD *dst, int dstStride)
{
    BYTE alpha[16];
    /* 64-bit explicit alpha: 16 × 4-bit values, scanline order. */
    for (int i = 0; i < 8; ++i) {
        BYTE b = src[i];
        BYTE a0 = (BYTE)((b      ) & 0x0F);
        BYTE a1 = (BYTE)((b >> 4) & 0x0F);
        /* 4-bit replicate to 8-bit: a8 = (a4 << 4) | a4 */
        alpha[2 * i]     = (BYTE)((a0 << 4) | a0);
        alpha[2 * i + 1] = (BYTE)((a1 << 4) | a1);
    }

    /* Color block at src[8..15] — same layout as DXT1 4-colour mode (no
     * transparent index). */
    const WORD c0w = (WORD)(src[8] | (src[9] << 8));
    const WORD c1w = (WORD)(src[10] | (src[11] << 8));
    const DWORD indices = (DWORD)src[12] | ((DWORD)src[13] << 8)
                        | ((DWORD)src[14] << 16) | ((DWORD)src[15] << 24);

    DWORD palette[4];
    palette[0] = UnpackRGB565(c0w);
    palette[1] = UnpackRGB565(c1w);
    {
        const DWORD p0 = palette[0], p1 = palette[1];
        DWORD r0 = (p0 >> 16) & 0xFF, g0 = (p0 >> 8) & 0xFF, b0 = p0 & 0xFF;
        DWORD r1 = (p1 >> 16) & 0xFF, g1 = (p1 >> 8) & 0xFF, b1 = p1 & 0xFF;
        palette[2] = 0xFF000000u
                   | (((2*r0 + r1) / 3) << 16)
                   | (((2*g0 + g1) / 3) <<  8)
                   |  ((2*b0 + b1) / 3);
        palette[3] = 0xFF000000u
                   | (((r0 + 2*r1) / 3) << 16)
                   | (((g0 + 2*g1) / 3) <<  8)
                   |  ((b0 + 2*b1) / 3);
    }

    for (int y = 0; y < 4; ++y) {
        DWORD *row = dst + y * dstStride;
        for (int x = 0; x < 4; ++x) {
            const int linIdx = 4 * y + x;
            const int colIdx = (indices >> (2 * linIdx)) & 0x3;
            const DWORD rgb = palette[colIdx] & 0x00FFFFFFu;
            row[x] = ((DWORD)alpha[linIdx] << 24) | rgb;
        }
    }
}

/* Decompress an entire DXT-N compressed image (raw blocks, no DDS header)
 * into an out-of-line RGBA8888 buffer that the caller must free.
 * Returns NULL on allocation failure. */
typedef void (*DecodeBlockFn)(const BYTE *src, DWORD *dst, int dstStride);

static DWORD *DecompressDXT(const BYTE *src, int width, int height,
                            int blockSize, DecodeBlockFn decoder)
{
    const int blocksX = (width  + 3) / 4;
    const int blocksY = (height + 3) / 4;
    const size_t outBytes = (size_t)width * (size_t)height * 4;

    DWORD *out = (DWORD *)malloc(outBytes);
    if (!out) return NULL;
    memset(out, 0, outBytes);

    for (int by = 0; by < blocksY; ++by) {
        for (int bx = 0; bx < blocksX; ++bx) {
            const BYTE *block = src + (by * blocksX + bx) * blockSize;
            DWORD *dstBlock = out + by * 4 * width + bx * 4;
            decoder(block, dstBlock, width);
        }
    }
    return out;
}

/* =========================================================================
 *  TexImage2D bridge — the entry point JKA actually calls.
 * ========================================================================= */
static void Bridge_UploadAsRGBA(GLenum target, GLint level,
                                 GLsizei width, GLsizei height,
                                 const DWORD *rgba)
{
    /* Forward to FakeGL's glTexImage2D.  FakeGL maps GL_RGBA + GL_UNSIGNED_BYTE
     * + internalformat=GL_RGBA to D3DFMT_A8R8G8B8 internally. */
    glTexImage2D(target, level, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

/* Resolve the FourCC of a DDS file.  Returns 0 if the input is not a valid
 * DDS file (no "DDS " magic, header too short for the buffer, etc.). */
static DWORD ParseDDSContainer(const void *pixels, size_t bufBytes,
                                int *outWidth, int *outHeight,
                                const BYTE **outData, size_t *outDataBytes)
{
    if (!pixels || bufBytes < sizeof(DWORD) + sizeof(DDS_HEADER)) return 0;

    const BYTE *p = (const BYTE *)pixels;
    DWORD magic;
    memcpy(&magic, p, sizeof(DWORD));
    if (magic != DDS_MAGIC) return 0;

    const DDS_HEADER *hdr = (const DDS_HEADER *)(p + sizeof(DWORD));
    if (outWidth)  *outWidth  = (int)hdr->dwWidth;
    if (outHeight) *outHeight = (int)hdr->dwHeight;
    if (outData)         *outData      = p + sizeof(DWORD) + sizeof(DDS_HEADER);
    if (outDataBytes)    *outDataBytes = bufBytes - sizeof(DWORD) - sizeof(DDS_HEADER);
    return hdr->ddspf.dwFourCC;
}

void APIENTRY QGLBridge_TexImage2D(GLenum target, GLint level, GLint internalformat,
                                   GLsizei width, GLsizei height, GLint border,
                                   GLenum format, GLenum type, const GLvoid *pixels)
{
    /* Fast path: standard core GL formats — forward directly to FakeGL. */
    const bool isDDSInternal =
        internalformat == GL_DDS1_EXT ||
        internalformat == GL_DDS5_EXT ||
        internalformat == GL_DDS_RGB16_EXT ||
        internalformat == GL_DDS_RGBA32_EXT;
    const bool isS3TCInternal =
        internalformat == GL_COMPRESSED_RGB_S3TC_DXT1_EXT  ||
        internalformat == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT ||
        internalformat == GL_COMPRESSED_RGBA_S3TC_DXT3_EXT ||
        internalformat == GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;

    if (!isDDSInternal && !isS3TCInternal) {
        glTexImage2D(target, level, internalformat, width, height, border,
                     format, type, pixels);
        return;
    }

    if (!pixels) {
        XBLog_Write("QGLBridge: TexImage2D called with NULL pixels for compressed format\n");
        return;
    }

    /* DDS container path: parse header, dispatch on FourCC. */
    if (isDDSInternal) {
        /* The caller's reported width/height is the image's logical size;
         * the DDS header's width/height should agree.  We trust the call
         * site's width/height for FakeGL's upload — the header is mostly
         * used to locate the data offset and identify the format. */
        int ddsW = width, ddsH = height;
        const BYTE *data = NULL;
        size_t dataBytes = 0;
        /* We don't know the buffer length from the GL call — assume it's
         * at least header + minimum data for the format.  Pass a generous
         * upper bound; ParseDDSContainer only reads what it needs. */
        const size_t roughMax = 4 + 124 + (size_t)width * height * 4 + 4096;
        DWORD fourcc = ParseDDSContainer(pixels, roughMax, &ddsW, &ddsH, &data, &dataBytes);
        if (fourcc == 0) {
            XBLog_Write("QGLBridge: DDS internal format but no 'DDS ' magic — ignoring\n");
            return;
        }

        DWORD *rgba = NULL;
        if (fourcc == FOURCC_DXT1 || internalformat == GL_DDS1_EXT) {
            rgba = DecompressDXT(data, ddsW, ddsH, 8, DecodeDXT1Block);
        } else if (fourcc == FOURCC_DXT3) {
            rgba = DecompressDXT(data, ddsW, ddsH, 16, DecodeDXT3Block);
        } else if (fourcc == FOURCC_DXT5 || internalformat == GL_DDS5_EXT) {
            rgba = DecompressDXT(data, ddsW, ddsH, 16, DecodeDXT5Block);
        } else if (internalformat == GL_DDS_RGB16_EXT) {
            /* RGB565 uncompressed.  Expand to RGBA8888. */
            const size_t outBytes = (size_t)ddsW * ddsH * 4;
            rgba = (DWORD *)malloc(outBytes);
            if (rgba) {
                const WORD *src = (const WORD *)data;
                for (int i = 0; i < ddsW * ddsH; ++i) {
                    rgba[i] = UnpackRGB565(src[i]);
                }
            }
        } else if (internalformat == GL_DDS_RGBA32_EXT) {
            /* Already 32bpp.  But the DDS file stores it as B,G,R,A typically.
             * For now treat it as already-RGBA — adjust if the colour swap
             * shows up wrong on screen. */
            const size_t outBytes = (size_t)ddsW * ddsH * 4;
            rgba = (DWORD *)malloc(outBytes);
            if (rgba) memcpy(rgba, data, outBytes);
        } else {
            XBLog_Write("QGLBridge: unhandled DDS FourCC, falling through to direct forward\n");
            glTexImage2D(target, level, internalformat, width, height, border,
                         format, type, pixels);
            return;
        }

        if (rgba) {
            Bridge_UploadAsRGBA(target, level, ddsW, ddsH, rgba);
            free(rgba);
        } else {
            XBLog_Write("QGLBridge: DXT decompress alloc failed\n");
        }
        return;
    }

    /* Raw S3TC blocks (no DDS header) path. */
    if (isS3TCInternal) {
        DWORD *rgba = NULL;
        switch (internalformat) {
        case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
        case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
            rgba = DecompressDXT((const BYTE *)pixels, width, height, 8, DecodeDXT1Block);
            break;
        case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
            rgba = DecompressDXT((const BYTE *)pixels, width, height, 16, DecodeDXT3Block);
            break;
        case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
            rgba = DecompressDXT((const BYTE *)pixels, width, height, 16, DecodeDXT5Block);
            break;
        }
        if (rgba) {
            Bridge_UploadAsRGBA(target, level, width, height, rgba);
            free(rgba);
        } else {
            XBLog_Write("QGLBridge: S3TC decompress alloc failed\n");
        }
    }
}

void APIENTRY QGLBridge_TexImage2DEXT(GLenum target, GLint level, GLint /*numlevels*/,
                                      GLint internalformat,
                                      GLsizei width, GLsizei height, GLint border,
                                      GLenum format, GLenum type, const GLvoid *pixels)
{
    /* qglTexImage2DEXT carries an extra "numlevels" parameter for explicit
     * mipmap-count uploads (JKA uses it for the DDS path in tr_image.cpp).
     * For the bridge's purposes we treat it as a regular TexImage2D — FakeGL
     * doesn't have a mipmap-count concept exposed at this API level.  Only
     * level 0 (the base image) is uploaded; mipmaps would need separate
     * subsequent calls with their own levels. */
    QGLBridge_TexImage2D(target, level, internalformat, width, height, border,
                         format, type, pixels);
}
