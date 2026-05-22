/*
 * glteximage_dds.cpp — Plan-B DDS/DXT texture upload bridge
 *
 * JKA stores its textures in DDS format with DXT1/DXT3/DXT5 compression
 * (and a few uncompressed DDS variants).  It calls glTexImage2D with
 * internalformat=GL_DDS1_EXT/GL_DDS5_EXT/GL_DDS_RGB16_EXT/etc., which
 * OpenJKDF2's fakeglx.cpp's GLToDXPixelFormat does not understand —
 * it returns E_FAIL and the texture is silently dropped.  Result on
 * SP_DoLicense: empty texture bound at stage 0, Present hangs in
 * CXBX-R LLE GPU on the malformed draw call.
 *
 * This file provides JkaGlTexImage2D — invoked via the #define-based
 * redirect in qgl_console.h.  It detects DDS internalformats, runs the
 * S3 Texture Compression reference DXT decompression algorithm to
 * produce RGBA8888 in CPU memory, and forwards to fakegl's actual
 * glTexImage2D as GL_RGBA — which fakegl handles natively.
 *
 * Adapted from the pre-Plan-A qgl_texture_bridge.cpp (which used the
 * qgl_* function pointer table that no longer exists).  The DXT decoder
 * is byte-identical to that earlier version — straight from the S3TC
 * spec, no algorithm changes.
 */

#include <xtl.h>
#include <stdlib.h>
#include <string.h>

/* qgl_console.h will define glTexImage2D -> JkaGlTexImage2D for JKA call
 * sites.  We need fakegl's REAL glTexImage2D here, so undef the redirect
 * locally before declaring the real signature. */
#define _JKA_DDS_BRIDGE_INTERNAL_
#include "../../renderer/qgl_console.h"
#undef glTexImage2D
extern "C" void glTexImage2D(GLenum target, GLint level, GLint internalformat,
                              GLsizei width, GLsizei height, GLint border,
                              GLenum format, GLenum type, const GLvoid *pixels);
extern "C" void XBLog_Write(const char *msg);
extern "C" int JkaFakeglUploadDDS(GLint internalformat, GLsizei width, GLsizei height,
                                   GLint mipcount, const GLvoid *pixels, DWORD pixelBytes);

static int s_jkaDdsUploadPicmip = 0;

extern "C" void JkaFakeglSetDDSUploadPicmip(int picmip)
{
    s_jkaDdsUploadPicmip = picmip > 0 ? picmip : 0;
}

/* ─── DXT decoder (S3 Texture Compression reference impl) ────────── */

static void UnpackRGB565(WORD c, DWORD *out)
{
    BYTE r = (BYTE)(((c >> 11) & 0x1F) * 255 / 31);
    BYTE g = (BYTE)(((c >>  5) & 0x3F) * 255 / 63);
    BYTE b = (BYTE)(( c        & 0x1F) * 255 / 31);
    *out = (0xFFu << 24) | (r << 16) | (g << 8) | b;
}

static void DecodeDXT1Block(const BYTE *src, DWORD *dst, int dstStride)
{
    WORD c0 = (WORD)(src[0] | (src[1] << 8));
    WORD c1 = (WORD)(src[2] | (src[3] << 8));
    DWORD palette[4];
    UnpackRGB565(c0, &palette[0]);
    UnpackRGB565(c1, &palette[1]);
    if (c0 > c1) {
        BYTE r0=(BYTE)(palette[0]>>16), g0=(BYTE)(palette[0]>>8), b0=(BYTE)palette[0];
        BYTE r1=(BYTE)(palette[1]>>16), g1=(BYTE)(palette[1]>>8), b1=(BYTE)palette[1];
        palette[2] = (0xFFu<<24)|(((2*r0+r1)/3)<<16)|(((2*g0+g1)/3)<<8)|((2*b0+b1)/3);
        palette[3] = (0xFFu<<24)|(((r0+2*r1)/3)<<16)|(((g0+2*g1)/3)<<8)|((b0+2*b1)/3);
    } else {
        BYTE r0=(BYTE)(palette[0]>>16), g0=(BYTE)(palette[0]>>8), b0=(BYTE)palette[0];
        BYTE r1=(BYTE)(palette[1]>>16), g1=(BYTE)(palette[1]>>8), b1=(BYTE)palette[1];
        palette[2] = (0xFFu<<24)|(((r0+r1)/2)<<16)|(((g0+g1)/2)<<8)|((b0+b1)/2);
        palette[3] = 0; /* transparent black */
    }
    DWORD bits = src[4] | (src[5]<<8) | (src[6]<<16) | (src[7]<<24);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            int idx = (bits >> (2*(y*4 + x))) & 3;
            dst[y*dstStride + x] = palette[idx];
        }
    }
}

static void DecodeDXT5AlphaBlock(const BYTE *src, BYTE *dst16)
{
    BYTE a0 = src[0], a1 = src[1];
    BYTE alpha[8];
    alpha[0]=a0; alpha[1]=a1;
    if (a0 > a1) {
        for (int i=2;i<8;++i) alpha[i] = (BYTE)(((8-i)*a0 + (i-1)*a1) / 7);
    } else {
        for (int i=2;i<6;++i) alpha[i] = (BYTE)(((6-i)*a0 + (i-1)*a1) / 5);
        alpha[6]=0; alpha[7]=255;
    }
    unsigned __int64 bits = 0;
    for (int i=0;i<6;++i) bits |= ((unsigned __int64)src[2+i]) << (8*i);
    for (int i=0;i<16;++i) dst16[i] = alpha[(bits >> (3*i)) & 7];
}

static void DecodeDXT5Block(const BYTE *src, DWORD *dst, int dstStride)
{
    BYTE alpha[16];
    DecodeDXT5AlphaBlock(src, alpha);
    /* DXT5 color is DXT1-like but always 4-color (no transparent). */
    WORD c0 = (WORD)(src[8] | (src[9] << 8));
    WORD c1 = (WORD)(src[10] | (src[11] << 8));
    DWORD palette[4];
    UnpackRGB565(c0, &palette[0]);
    UnpackRGB565(c1, &palette[1]);
    BYTE r0=(BYTE)(palette[0]>>16), g0=(BYTE)(palette[0]>>8), b0=(BYTE)palette[0];
    BYTE r1=(BYTE)(palette[1]>>16), g1=(BYTE)(palette[1]>>8), b1=(BYTE)palette[1];
    palette[2] = (((2*r0+r1)/3)<<16)|(((2*g0+g1)/3)<<8)|((2*b0+b1)/3);
    palette[3] = (((r0+2*r1)/3)<<16)|(((g0+2*g1)/3)<<8)|((b0+2*b1)/3);
    DWORD bits = src[12] | (src[13]<<8) | (src[14]<<16) | (src[15]<<24);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            int idx = (bits >> (2*(y*4 + x))) & 3;
            DWORD c = palette[idx] & 0x00FFFFFFu;
            DWORD a = alpha[y*4 + x];
            dst[y*dstStride + x] = (a << 24) | c;
        }
    }
}

static void DecodeDXT3Block(const BYTE *src, DWORD *dst, int dstStride)
{
    /* DXT3 = explicit 4-bit alpha (8 bytes) + DXT1-style color (8 bytes) */
    BYTE alpha[16];
    for (int i = 0; i < 8; ++i) {
        BYTE byte = src[i];
        alpha[i*2  ] = (BYTE)((byte & 0x0F) | ((byte & 0x0F) << 4));
        alpha[i*2+1] = (BYTE)((byte & 0xF0) | ((byte & 0xF0) >> 4));
    }
    WORD c0 = (WORD)(src[8] | (src[9] << 8));
    WORD c1 = (WORD)(src[10] | (src[11] << 8));
    DWORD palette[4];
    UnpackRGB565(c0, &palette[0]);
    UnpackRGB565(c1, &palette[1]);
    BYTE r0=(BYTE)(palette[0]>>16), g0=(BYTE)(palette[0]>>8), b0=(BYTE)palette[0];
    BYTE r1=(BYTE)(palette[1]>>16), g1=(BYTE)(palette[1]>>8), b1=(BYTE)palette[1];
    palette[2] = (((2*r0+r1)/3)<<16)|(((2*g0+g1)/3)<<8)|((2*b0+b1)/3);
    palette[3] = (((r0+2*r1)/3)<<16)|(((g0+2*g1)/3)<<8)|((b0+2*b1)/3);
    DWORD bits = src[12] | (src[13]<<8) | (src[14]<<16) | (src[15]<<24);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            int idx = (bits >> (2*(y*4 + x))) & 3;
            DWORD c = palette[idx] & 0x00FFFFFFu;
            DWORD a = alpha[y*4 + x];
            dst[y*dstStride + x] = (a << 24) | c;
        }
    }
}

typedef void (*DXTBlockDecodeFn)(const BYTE *src, DWORD *dst, int dstStride);

static DWORD *DecompressDXT(const BYTE *src, int width, int height,
                            int blockBytes, DXTBlockDecodeFn decode)
{
    DWORD *rgba = (DWORD*)malloc((size_t)width * height * 4);
    if (!rgba) return NULL;
    int blocksW = (width + 3) / 4;
    int blocksH = (height + 3) / 4;
    for (int by = 0; by < blocksH; ++by) {
        for (int bx = 0; bx < blocksW; ++bx) {
            decode(src, rgba + (by*4) * width + bx*4, width);
            src += blockBytes;
        }
    }
    return rgba;
}

static DWORD *DownsampleRGBA2x(const DWORD *src, int width, int height,
                               int *outWidth, int *outHeight)
{
    int newWidth = (width > 1) ? (width >> 1) : 1;
    int newHeight = (height > 1) ? (height >> 1) : 1;
    DWORD *dst = (DWORD*)malloc((size_t)newWidth * newHeight * sizeof(DWORD));
    if (!dst) return NULL;

    for (int y = 0; y < newHeight; ++y) {
        for (int x = 0; x < newWidth; ++x) {
            int sx = x << 1;
            int sy = y << 1;
            const DWORD c0 = src[sy * width + sx];
            const DWORD c1 = src[sy * width + ((sx + 1 < width) ? sx + 1 : sx)];
            const DWORD c2 = src[((sy + 1 < height) ? sy + 1 : sy) * width + sx];
            const DWORD c3 = src[((sy + 1 < height) ? sy + 1 : sy) * width + ((sx + 1 < width) ? sx + 1 : sx)];
            DWORD a = ((c0 >> 24) + (c1 >> 24) + (c2 >> 24) + (c3 >> 24)) >> 2;
            DWORD r = (((c0 >> 16) & 0xff) + ((c1 >> 16) & 0xff) + ((c2 >> 16) & 0xff) + ((c3 >> 16) & 0xff)) >> 2;
            DWORD g = (((c0 >> 8) & 0xff) + ((c1 >> 8) & 0xff) + ((c2 >> 8) & 0xff) + ((c3 >> 8) & 0xff)) >> 2;
            DWORD b = ((c0 & 0xff) + (c1 & 0xff) + (c2 & 0xff) + (c3 & 0xff)) >> 2;
            dst[y * newWidth + x] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }

    *outWidth = newWidth;
    *outHeight = newHeight;
    return dst;
}

static void ClampXboxTexture(DWORD **rgba, int *width, int *height)
{
#ifdef _XBOX
    const int maxXboxTextureSize = 128;
    while (*rgba && (*width > maxXboxTextureSize || *height > maxXboxTextureSize)) {
        int newWidth, newHeight;
        DWORD *small = DownsampleRGBA2x(*rgba, *width, *height, &newWidth, &newHeight);
        if (!small) {
            break;
        }
        free(*rgba);
        *rgba = small;
        *width = newWidth;
        *height = newHeight;
    }
#endif
}

#define FOURCC_DXT1 0x31545844u  /* 'DXT1' little-endian */
#define FOURCC_DXT3 0x33545844u
#define FOURCC_DXT5 0x35545844u

/* DDS file container: "DDS " magic + 124-byte header.  We need the fourCC
 * to identify DXT variant and the pixel-data offset.  Layout copied from
 * Microsoft DDS spec. */
static DWORD DDSLevelBytes(DWORD fourcc, int width, int height, int bpp)
{
    if (fourcc == FOURCC_DXT1)
        return (DWORD)(((width + 3) / 4) * ((height + 3) / 4) * 8);
    if (fourcc == FOURCC_DXT3 || fourcc == FOURCC_DXT5)
        return (DWORD)(((width + 3) / 4) * ((height + 3) / 4) * 16);
    return (DWORD)(width * height * bpp);
}

static DWORD DDSPayloadBytes(DWORD fourcc, int width, int height, int mipcount, int bpp)
{
    DWORD bytes = 0;
    int levels = (mipcount > 0) ? mipcount : 1;
    int w = width;
    int h = height;
    for (int i = 0; i < levels; ++i) {
        bytes += DDSLevelBytes(fourcc, w, h, bpp);
        if (w > 1) w >>= 1;
        if (h > 1) h >>= 1;
    }
    return bytes;
}

static DWORD ParseDDSContainer(const void *pixels,
                                int *outW, int *outH, int *outMipCount,
                                int *outBpp, const BYTE **outData)
{
    const BYTE *p = (const BYTE*)pixels;
    if (memcmp(p, "DDS ", 4) != 0) return 0;
    DWORD height = *(const DWORD*)(p + 12);
    DWORD width  = *(const DWORD*)(p + 16);
    DWORD mipcount = *(const DWORD*)(p + 28);
    DWORD pfFlags = *(const DWORD*)(p + 80);
    DWORD fourcc = (pfFlags & 0x4 /*DDPF_FOURCC*/) ? *(const DWORD*)(p + 84) : 0;
    DWORD rgbBits = *(const DWORD*)(p + 88);
    *outW = (int)width;
    *outH = (int)height;
    *outMipCount = (int)mipcount;
    *outBpp = (int)(rgbBits / 8);
    *outData = p + 128;
    return fourcc;
}

/* JKA's GL_DDS* enums from qgl_console.h JKA-extensions block:
 *   GL_DDS1_EXT       0x9995  (DXT1 RGB)
 *   GL_DDS5_EXT       0x9996  (DXT5 RGBA)
 *   GL_DDS_RGB16_EXT  0x9997  (uncompressed 16bpp)
 *   GL_DDS_RGBA32_EXT 0x9998  (uncompressed 32bpp)
 * Plus standard S3TC:
 *   GL_COMPRESSED_RGB_S3TC_DXT1_EXT   0x83F0
 *   GL_COMPRESSED_RGBA_S3TC_DXT1_EXT  0x83F1
 *   GL_COMPRESSED_RGBA_S3TC_DXT3_EXT  0x83F2
 *   GL_COMPRESSED_RGBA_S3TC_DXT5_EXT  0x83F3
 */
static void JkaGlTexImage2DWithLevels(GLenum target, GLint level, GLint numlevels,
                                      GLint internalformat, GLsizei width, GLsizei height,
                                      GLint border, GLenum format, GLenum type,
                                      const GLvoid *pixels)
{
    /* Pass through anything that's already a standard GL format. */
    if (internalformat == GL_RGBA || internalformat == GL_RGB ||
        internalformat == GL_LUMINANCE || internalformat == GL_ALPHA ||
        internalformat == GL_LUMINANCE_ALPHA)
    {
        glTexImage2D(target, level, internalformat, width, height, border,
                     format, type, pixels);
        return;
    }

    /* DDS / DXT formats — decompress to RGBA8888 in CPU memory. */
    if (!pixels) {
        glTexImage2D(target, level, GL_RGBA, width, height, border,
                     GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        return;
    }

    DWORD *rgba = NULL;
    int dW = width, dH = height;

    /* JKA's GL_DDS1_EXT / GL_DDS5_EXT inputs are full DDS containers
     * (header + data).  Standard S3TC inputs are just raw block data. */
    if (internalformat == 0x9995 /*GL_DDS1_EXT*/ ||
        internalformat == 0x9996 /*GL_DDS5_EXT*/ ||
        internalformat == 0x9997 /*GL_DDS_RGB16_EXT*/ ||
        internalformat == 0x9998 /*GL_DDS_RGBA32_EXT*/)
    {
        const BYTE *data;
        int ddsMipCount, ddsBpp;
        DWORD fourcc = ParseDDSContainer(pixels, &dW, &dH, &ddsMipCount, &ddsBpp, &data);
        if (fourcc == 0 && format == GL_RGBA && type == GL_UNSIGNED_BYTE) {
            /* R_CreateBuiltinImages seeds *savegame with an ordinary RGBA
             * scratch buffer but tags it GL_DDS1_EXT so later savegame loads
             * can replace it.  Do not feed that placeholder into fakegl as a
             * compressed DDS upload. */
            XBLog_Write("JkaGlTexImage2D: DDS tag without DDS header; uploading RGBA placeholder");
            glTexImage2D(target, level, GL_RGBA, width, height, border,
                         GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            return;
        }
#ifdef _XBOX
        if (fourcc == FOURCC_DXT1 || fourcc == FOURCC_DXT3 || fourcc == FOURCC_DXT5) {
            int levels = (numlevels > 0) ? numlevels : ddsMipCount;
            if (levels <= 0) levels = 1;
            int picmip = s_jkaDdsUploadPicmip;
            while (picmip > 0 && levels > 1 && dW > 1 && dH > 1) {
                DWORD topBytes = DDSLevelBytes(fourcc, dW, dH, ddsBpp);
                data += topBytes;
                if (dW > 1) dW >>= 1;
                if (dH > 1) dH >>= 1;
                --levels;
                --picmip;
            }
            if (picmip != s_jkaDdsUploadPicmip) {
                XBLog_Write("JkaGlTexImage2D: DDS picmip applied before direct upload");
            }
            DWORD payloadBytes = DDSPayloadBytes(fourcc, dW, dH, levels, ddsBpp);
            if (JkaFakeglUploadDDS(internalformat, dW, dH, levels, data, payloadBytes)) {
                return;
            }
            XBLog_Write("JkaGlTexImage2D: direct DDS upload failed; skipping RGBA decode on Xbox");
            return;
        }
#endif
        if (fourcc == FOURCC_DXT1)
            rgba = DecompressDXT(data, dW, dH, 8, DecodeDXT1Block);
        else if (fourcc == FOURCC_DXT3)
            rgba = DecompressDXT(data, dW, dH, 16, DecodeDXT3Block);
        else if (fourcc == FOURCC_DXT5)
            rgba = DecompressDXT(data, dW, dH, 16, DecodeDXT5Block);
        else if (internalformat == 0x9997) {
            rgba = (DWORD*)malloc((size_t)dW * dH * sizeof(DWORD));
            if (rgba) {
                const WORD *src = (const WORD*)data;
                for (int i = 0; i < dW * dH; ++i) {
                    UnpackRGB565(src[i], &rgba[i]);
                }
#ifdef _XBOX
                XBLog_Write("JkaGlTexImage2D: converted RGB565 DDS to RGBA");
#endif
            }
        }
        else if (internalformat == 0x9998) {
            rgba = (DWORD*)malloc((size_t)dW * dH * sizeof(DWORD));
            if (rgba) {
                memcpy(rgba, data, (size_t)dW * dH * sizeof(DWORD));
#ifdef _XBOX
                XBLog_Write("JkaGlTexImage2D: converted RGBA32 DDS to regular RGBA upload");
#endif
            }
        }
        else if (internalformat == 0x9997 || internalformat == 0x9998) {
            /* Uncompressed DDS — just copy past the header */
            int bpp = (internalformat == 0x9997) ? 2 : 4;
            size_t bytes = (size_t)dW * dH * bpp;
            rgba = (DWORD*)malloc(bytes);
            if (rgba) memcpy(rgba, data, bytes);
        }
    } else {
        /* Standard S3TC: raw blocks, no DDS header */
        if (internalformat == 0x83F0 /*DXT1 RGB*/ || internalformat == 0x83F1 /*DXT1 RGBA*/)
            rgba = DecompressDXT((const BYTE*)pixels, width, height, 8, DecodeDXT1Block);
        else if (internalformat == 0x83F2 /*DXT3*/)
            rgba = DecompressDXT((const BYTE*)pixels, width, height, 16, DecodeDXT3Block);
        else if (internalformat == 0x83F3 /*DXT5*/)
            rgba = DecompressDXT((const BYTE*)pixels, width, height, 16, DecodeDXT5Block);
    }

    if (rgba) {
        ClampXboxTexture(&rgba, &dW, &dH);
        glTexImage2D(target, level, GL_RGBA, dW, dH, border,
                     GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        free(rgba);
    } else {
        /* Unknown format — pass through to fakegl (it'll E_FAIL but at
         * least we don't silently crash). */
        glTexImage2D(target, level, internalformat, width, height, border,
                     format, type, pixels);
    }
}

extern "C" void JkaGlTexImage2D(GLenum target, GLint level, GLint internalformat,
                                 GLsizei width, GLsizei height, GLint border,
                                 GLenum format, GLenum type, const GLvoid *pixels)
{
    JkaGlTexImage2DWithLevels(target, level, 0, internalformat, width, height,
                              border, format, type, pixels);
}

/* JKA also calls qglTexImage2DEXT (the mipmapped variant) via our compat
 * declaration in qgl_console.h.  Forward through the DDS-aware path. */
extern "C" void JkaGlTexImage2DEXT(GLenum target, GLint level, GLint numlevels,
                                    GLint internalformat, GLsizei width, GLsizei height,
                                    GLint border, GLenum format, GLenum type,
                                    const GLvoid *pixels)
{
    JkaGlTexImage2DWithLevels(target, level, numlevels, internalformat, width, height,
                              border, format, type, pixels);
}
