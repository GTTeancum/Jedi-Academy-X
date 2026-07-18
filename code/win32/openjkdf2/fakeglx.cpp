/*
Copyright (C) 2000 Jack Palevich.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

// fakeglx.cpp - Uses Direct3D to implement a subset of OpenGL.

#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#ifdef _XBOX
#define _JKA_DDS_BRIDGE_INTERNAL_
#endif
#include "fakeglx.h"
#include "../xb_log.h"
#ifdef _XBOX
extern "C" void* JkaStaticTextureAlloc(unsigned long size, GLuint texNum);
extern "C" unsigned long JkaStaticTextureUsed(void);
extern "C" unsigned long JkaStaticTextureFree(void);
extern "C" unsigned long JkaStaticTextureCapacity(void);
#endif

// TODO: Fix this warning instead of disableing it
#pragma warning( disable : 4244 )
#pragma warning( disable : 4820 )
#pragma warning( disable : 4273 )

#define     D3D_OVERLOADS
#define     RELEASENULL(object) if (object) {object->Release();}

#include "xgraphics.h"

//#define PROFILE
#ifdef PROFILE
#pragma pack(push, 8)       // Make sure structure packing is set properly
#include <d3d8perf.h>
#pragma pack(pop)
#endif

// Some DX7 helper functions that we're still using with DX8
#ifdef D3DRGBA
#undef D3DRGBA
#endif
#define D3DRGBA                                 D3DCOLOR_COLORVALUE

#define D3DRGB(_r,_g,_b)                        D3DCOLOR_COLORVALUE(_r,_g,_b,1.f)

#define RGBA_MAKE                               D3DCOLOR_RGBA

#define TEXTURE0_SGIS							0x835E
#define TEXTURE1_SGIS							0x835F
#define D3D_TEXTURE_MAXANISOTROPY				0xf70001

#ifndef GL_POLYGON_OFFSET_FILL
#define GL_POLYGON_OFFSET_FILL					0x8037
#endif
#ifndef GL_CLIP_PLANE0
#define GL_CLIP_PLANE0							0x3000
#endif
#ifndef GL_ADD
#define GL_ADD									0x0104
#endif
#ifndef GL_EXP
#define GL_EXP									0x0800
#endif
#ifndef GL_EXP2
#define GL_EXP2									0x0801
#endif

void LocalDebugBreak()
{
	// Not needed atm
	//DebugBreak();
}

// Globals
bool g_force16bitTextures = true;
DWORD gWidth = 640;
DWORD gHeight = 480;
int bytes = 4;

//0 = interlaced 480i
//1 = progressive ("HD") 480p, 720p, depends on dashboard settings
int gVideoMode = 0;

class FakeGL;
static FakeGL* gFakeGL;

#ifdef _XBOX
extern "C" D3DTexture* WINAPI D3DDevice_CreateTexture2(DWORD Width, DWORD Height, DWORD Depth, DWORD Levels, DWORD Usage, D3DFORMAT Format, D3DRESOURCETYPE D3DType);
static DWORD g_fakeglTextureCount = 0;
static DWORD g_fakeglTextureFailures = 0;
static unsigned __int64 g_fakeglTextureBytes = 0;
static DWORD g_fakeglRegisteredTextureCount = 0;
static DWORD g_fakeglRegisteredTextureDenied = 0;
static unsigned __int64 g_fakeglRegisteredTextureBytes = 0;
static const DWORD FAKEGL_REGISTERED_TEXTURE_SOFT_CAP = 7 * 1024 * 1024;
static const DWORD FAKEGL_REGISTERED_TEXTURE_MIN_FREE = 512 * 1024;
static bool g_stefxSkipSwapBlockUntilIdle = false;
static int g_stefxFakeglSwapFrame = 0;
static char g_stefxFakeglTextureDebugName[128] = "<none>";
extern "C" volatile unsigned int g_SPXBFakeGLPrimitiveCalls;
extern "C" volatile unsigned int g_SPXBFakeGLPrimitiveVerts;
extern "C" volatile unsigned int g_SPXBFakeGLStateFlushes;
extern "C" volatile unsigned int g_SPXBFramebufferData;
extern "C" volatile unsigned int g_SPXBFramebufferPitch;
extern "C" volatile unsigned int g_SPXBFramebufferWidth;
extern "C" volatile unsigned int g_SPXBFramebufferHeight;
extern "C" volatile unsigned int g_SPXBFramebufferFormat;
extern "C" volatile unsigned int g_SPXBFramebufferSize;

extern "C" void JkaFakeglSetTextureDebugName(const char *name)
{
	int i;
	if (!name || !name[0])
	{
		name = "<none>";
	}
	strncpy(g_stefxFakeglTextureDebugName, name, sizeof(g_stefxFakeglTextureDebugName) - 1);
	g_stefxFakeglTextureDebugName[sizeof(g_stefxFakeglTextureDebugName) - 1] = '\0';
	for (i = 0; g_stefxFakeglTextureDebugName[i]; ++i)
	{
		if (g_stefxFakeglTextureDebugName[i] == '\n' ||
			g_stefxFakeglTextureDebugName[i] == '\r')
		{
			g_stefxFakeglTextureDebugName[i] = ' ';
		}
	}
}

extern "C" const char *JkaFakeglGetTextureDebugName(void)
{
	return g_stefxFakeglTextureDebugName[0] ? g_stefxFakeglTextureDebugName : "<none>";
}

static const char *FakeGL_CurrentTextureDebugName(void)
{
	return JkaFakeglGetTextureDebugName();
}

static bool FakeGL_ShouldTraceDDSImage(const char *name)
{
	if (!name)
	{
		return false;
	}
	return strstr(name, "textures/borg/bars") ||
		strstr(name, "textures/borg/bars2") ||
		strstr(name, "textures/borg/basic1") ||
		strstr(name, "textures/borg/borgladder") ||
		strstr(name, "env/junk_");
}

extern "C" void FakeGL_ResetRegisteredTextureBudget(void)
{
	if (g_fakeglRegisteredTextureCount || g_fakeglRegisteredTextureBytes || g_fakeglRegisteredTextureDenied)
	{
		XBLF("JA: fakegl registered texture budget reset count=%u regKB=%u denied=%u",
			(unsigned int)g_fakeglRegisteredTextureCount,
			(unsigned int)(g_fakeglRegisteredTextureBytes / 1024),
			(unsigned int)g_fakeglRegisteredTextureDenied);
	}

	g_fakeglRegisteredTextureCount = 0;
	g_fakeglRegisteredTextureBytes = 0;
	g_fakeglRegisteredTextureDenied = 0;
}

static void FakeGL_WriteLe16(BYTE *dst, WORD value)
{
	dst[0] = (BYTE)(value & 0xff);
	dst[1] = (BYTE)((value >> 8) & 0xff);
}

static void FakeGL_WriteLe32(BYTE *dst, DWORD value)
{
	dst[0] = (BYTE)(value & 0xff);
	dst[1] = (BYTE)((value >> 8) & 0xff);
	dst[2] = (BYTE)((value >> 16) & 0xff);
	dst[3] = (BYTE)((value >> 24) & 0xff);
}

static bool FakeGL_WriteAll(HANDLE file, const void *data, DWORD bytesToWrite)
{
	const BYTE *cursor = (const BYTE *)data;
	while (bytesToWrite > 0)
	{
		DWORD written = 0;
		if (!WriteFile(file, cursor, bytesToWrite, &written, NULL) || written == 0)
		{
			return false;
		}
		cursor += written;
		bytesToWrite -= written;
	}
	return true;
}

typedef struct _FGL_STR
{
	unsigned short Length;
	unsigned short MaximumLength;
	char *Buffer;
} FGL_STR;

typedef struct _FGL_OA
{
	HANDLE RootDirectory;
	FGL_STR *ObjectName;
	unsigned long Attributes;
} FGL_OA;

typedef struct _FGL_IOSB
{
	long Status;
	unsigned long Information;
} FGL_IOSB;

extern "C" long __stdcall NtCreateFile(HANDLE*, unsigned long, FGL_OA*, FGL_IOSB*,
	void*, unsigned long, unsigned long, unsigned long, unsigned long);
extern "C" long __stdcall NtWriteFile(HANDLE, HANDLE, void*, void*, FGL_IOSB*,
	void*, unsigned long, void*);
extern "C" long __stdcall NtFlushBuffersFile(HANDLE, FGL_IOSB*);
extern "C" long __stdcall NtClose(HANDLE);

static long FakeGL_NtCreateOverwrite(const char *path, HANDLE *out)
{
	FGL_STR name;
	FGL_OA oa;
	FGL_IOSB iosb;
	name.Buffer = (char *)path;
	name.Length = (unsigned short)strlen(path);
	name.MaximumLength = name.Length + 1;
	oa.RootDirectory = NULL;
	oa.ObjectName = &name;
	oa.Attributes = 0x40;
	*out = INVALID_HANDLE_VALUE;
	return NtCreateFile(out,
		GENERIC_WRITE | 0x00100000,
		&oa,
		&iosb,
		NULL,
		FILE_ATTRIBUTE_NORMAL,
		0,
		5,
		0x20 | 0x02 | 0x40);
}

static bool FakeGL_NtWriteAll(HANDLE file, const void *data, DWORD bytesToWrite)
{
	const BYTE *cursor = (const BYTE *)data;
	while (bytesToWrite > 0)
	{
		FGL_IOSB iosb;
		DWORD chunk = bytesToWrite;
		if (chunk > 0x10000)
		{
			chunk = 0x10000;
		}
		if (NtWriteFile(file, NULL, NULL, NULL, &iosb, (void *)cursor, chunk, NULL) < 0)
		{
			return false;
		}
		cursor += chunk;
		bytesToWrite -= chunk;
	}
	return true;
}

static bool FakeGL_FileExists(const char *path)
{
	DWORD attrs = GetFileAttributesA(path);
	return attrs != 0xffffffffu && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static bool FakeGL_FileExistsOnAnyRuntimeDrive(const char *filename)
{
	char path[96];
	static const char drives[] = { 'D', 'E', 'T' };
	int i;

	for (i = 0; i < (int)(sizeof(drives) / sizeof(drives[0])); ++i)
	{
		sprintf(path, "%c:\\%s", drives[i], filename);
		if (FakeGL_FileExists(path))
		{
			return true;
		}
	}
	return false;
}

#if defined(STEFX_ELITE_FORCE_MP)
#define STEFX_SCREENSHOT_PREFIX "ef_mp"
#else
#define STEFX_SCREENSHOT_PREFIX "ef_sp"
#endif

#define STEFX_SCREENSHOT_REQUEST_FILE STEFX_SCREENSHOT_PREFIX "_screenshot_request.txt"
#define STEFX_SCREENSHOT_PREOPEN_FILE STEFX_SCREENSHOT_PREFIX "_screenshot_preopen.txt"
#define STEFX_SCREENSHOT_BACKBUFFER_FILE STEFX_SCREENSHOT_PREFIX "_backbuffer.bmp"
#define STEFX_SCREENSHOT_XGSHOT_FILE STEFX_SCREENSHOT_PREFIX "_xgshot.bmp"
#define STEFX_RENDERPROBE_FILE STEFX_SCREENSHOT_PREFIX "_renderprobe.txt"
#define STEFX_CXBX_PRESENT_THROTTLE_FILE STEFX_SCREENSHOT_PREFIX "_cxbx_present_throttle.txt"

static const char *FakeGL_FindScreenshotRequestPath(void)
{
	int i;
	static const char *paths[] =
	{
		"D:\\" STEFX_SCREENSHOT_REQUEST_FILE,
		"E:\\" STEFX_SCREENSHOT_REQUEST_FILE,
		"T:\\" STEFX_SCREENSHOT_REQUEST_FILE,
		STEFX_SCREENSHOT_REQUEST_FILE,
		NULL
	};

	for (i = 0; paths[i]; ++i)
	{
		if (FakeGL_FileExists(paths[i]))
		{
			return paths[i];
		}
	}
	return NULL;
}

static void FakeGL_DeleteScreenshotRequests(void)
{
	int i;
	static const char *paths[] =
	{
		"D:\\" STEFX_SCREENSHOT_REQUEST_FILE,
		"E:\\" STEFX_SCREENSHOT_REQUEST_FILE,
		"T:\\" STEFX_SCREENSHOT_REQUEST_FILE,
		STEFX_SCREENSHOT_REQUEST_FILE,
		NULL
	};

	for (i = 0; paths[i]; ++i)
	{
		DeleteFileA(paths[i]);
	}
}

static bool FakeGL_ScreenshotRequested(void)
{
	return FakeGL_FindScreenshotRequestPath() != NULL;
}

static void FakeGL_PixelToRgb(const BYTE *src, bool rgb565, BYTE *r, BYTE *g, BYTE *b)
{
	if (rgb565)
	{
		WORD pixel = *(const WORD *)src;
		*r = (BYTE)((((pixel >> 11) & 31) * 255) / 31);
		*g = (BYTE)((((pixel >> 5) & 63) * 255) / 63);
		*b = (BYTE)(((pixel & 31) * 255) / 31);
	}
	else
	{
		*b = src[0];
		*g = src[1];
		*r = src[2];
	}
}

static DWORD FakeGL_SurfaceBytesPerPixel(D3DFORMAT format)
{
	if (format == D3DFMT_R5G6B5 || format == D3DFMT_LIN_R5G6B5)
	{
		return 2;
	}
	return 4;
}

static bool FakeGL_SurfaceHasVisibleSignal(const BYTE *pixels, D3DFORMAT format, DWORD width, DWORD height, DWORD pitch, DWORD *outVisibleSamples, DWORD *outMaxBrightness)
{
	const bool rgb565 = format == D3DFMT_R5G6B5 || format == D3DFMT_LIN_R5G6B5;
	const DWORD bytesPerPixel = FakeGL_SurfaceBytesPerPixel(format);
	DWORD visibleSamples = 0;
	DWORD maxBrightness = 0;

	if (outVisibleSamples)
	{
		*outVisibleSamples = 0;
	}
	if (outMaxBrightness)
	{
		*outMaxBrightness = 0;
	}

	__try
	{
		if (!pixels || width == 0 || height == 0 || pitch < width * bytesPerPixel)
		{
			return false;
		}

		for (DWORD sy = 0; sy < 12; ++sy)
		{
			DWORD y = (height - 1) * sy / 11;
			for (DWORD sx = 0; sx < 16; ++sx)
			{
				DWORD x = (width - 1) * sx / 15;
				const BYTE *src = pixels + y * pitch + x * bytesPerPixel;
				BYTE r, g, b;
				DWORD brightness;

				FakeGL_PixelToRgb(src, rgb565, &r, &g, &b);
				brightness = (DWORD)r + (DWORD)g + (DWORD)b;
				if (brightness > maxBrightness)
				{
					maxBrightness = brightness;
				}
				if (brightness > 8)
				{
					++visibleSamples;
				}
			}
		}
	}
	__except(EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}

	if (outVisibleSamples)
	{
		*outVisibleSamples = visibleSamples;
	}
	if (outMaxBrightness)
	{
		*outMaxBrightness = maxBrightness;
	}

	return visibleSamples >= 8 || maxBrightness >= 96;
}

static void FakeGL_LogSurfaceSample(const char *label, const BYTE *pixels, D3DFORMAT format, DWORD width, DWORD height, DWORD pitch)
{
	const bool rgb565 = format == D3DFMT_R5G6B5 || format == D3DFMT_LIN_R5G6B5;
	const DWORD bytesPerPixel = FakeGL_SurfaceBytesPerPixel(format);
	DWORD nonzero = 0;
	DWORD checksum = 0;
	DWORD firstPixel = 0;
	DWORD centerPixel = 0;
	DWORD cornerPixel = 0;

	__try
	{
		if (pixels && pitch >= width * bytesPerPixel && width > 0 && height > 0)
		{
			DWORD sy;
			for (sy = 0; sy < 4; ++sy)
			{
				DWORD y = (height - 1) * sy / 3;
				DWORD sx;
				for (sx = 0; sx < 4; ++sx)
				{
					DWORD x = (width - 1) * sx / 3;
					const BYTE *src = pixels + y * pitch + x * bytesPerPixel;
					DWORD pixel = rgb565 ? (DWORD)(*(const WORD *)src) : *(const DWORD *)src;
					BYTE r, g, b;
					FakeGL_PixelToRgb(src, rgb565, &r, &g, &b);
					if (r || g || b)
					{
						nonzero++;
					}
					checksum = (checksum * 1664525u) + pixel + 1013904223u;
				}
			}
			firstPixel = rgb565 ? (DWORD)(*(const WORD *)pixels) : *(const DWORD *)pixels;
			centerPixel = rgb565
				? (DWORD)(*(const WORD *)(pixels + (height / 2) * pitch + (width / 2) * bytesPerPixel))
				: *(const DWORD *)(pixels + (height / 2) * pitch + (width / 2) * bytesPerPixel);
			cornerPixel = rgb565
				? (DWORD)(*(const WORD *)(pixels + (height - 1) * pitch + (width - 1) * bytesPerPixel))
				: *(const DWORD *)(pixels + (height - 1) * pitch + (width - 1) * bytesPerPixel);
		}
	}
	__except(EXCEPTION_EXECUTE_HANDLER)
	{
		checksum = 0xffffffffu;
	}

	XBLF("STEFX: renderer screenshot sample label='%s' data=%p pitch=%u size=%ux%u fmt=0x%08x bpp=%u nonzero=%u checksum=0x%08x first=0x%08x center=0x%08x corner=0x%08x\n",
		label,
		pixels,
		(unsigned int)pitch,
		(unsigned int)width,
		(unsigned int)height,
		(unsigned int)format,
		(unsigned int)bytesPerPixel,
		(unsigned int)nonzero,
		(unsigned int)checksum,
		(unsigned int)firstPixel,
		(unsigned int)centerPixel,
		(unsigned int)cornerPixel);
}

static void FakeGL_LogSurfaceImage(const char *label, const BYTE *pixels, D3DFORMAT format, DWORD width, DWORD height, DWORD pitch)
{
	const bool rgb565 =
		format == D3DFMT_R5G6B5 ||
		format == D3DFMT_LIN_R5G6B5;
	const DWORD bytesPerPixel = FakeGL_SurfaceBytesPerPixel(format);
	const DWORD outWidth = 128;
	const DWORD outHeight = 96;
	const DWORD chunkPixels = 32;
	static const char hexDigits[] = "0123456789abcdef";

	if (!pixels)
	{
		XBLF("STEFX: renderer screenshot log skipped label='%s' pixels=NULL", label);
		return;
	}

	XBLF("STEFX: renderer screenshot log begin label='%s' src=%ux%u out=%ux%u pitch=%u fmt=0x%08x bpp=%u\n",
		label,
		(unsigned int)width,
		(unsigned int)height,
		(unsigned int)outWidth,
		(unsigned int)outHeight,
		(unsigned int)pitch,
		(unsigned int)format,
		(unsigned int)bytesPerPixel);

	for (DWORD y = 0; y < outHeight; ++y)
	{
		DWORD srcY = (y * height) / outHeight;
		for (DWORD x = 0; x < outWidth; x += chunkPixels)
		{
			char data[chunkPixels * 6 + 1];
			DWORD dataPos = 0;
			DWORD pixelsThisChunk = chunkPixels;
			if (x + pixelsThisChunk > outWidth)
			{
				pixelsThisChunk = outWidth - x;
			}
			for (DWORD n = 0; n < pixelsThisChunk; ++n)
			{
				DWORD srcX = ((x + n) * width) / outWidth;
				const BYTE *src = pixels + srcY * pitch + srcX * bytesPerPixel;
				BYTE r, g, b;
				FakeGL_PixelToRgb(src, rgb565, &r, &g, &b);
				data[dataPos++] = hexDigits[(r >> 4) & 0xf];
				data[dataPos++] = hexDigits[r & 0xf];
				data[dataPos++] = hexDigits[(g >> 4) & 0xf];
				data[dataPos++] = hexDigits[g & 0xf];
				data[dataPos++] = hexDigits[(b >> 4) & 0xf];
				data[dataPos++] = hexDigits[b & 0xf];
			}
			data[dataPos] = '\0';
			XBLF("STEFX: renderer screenshot log chunk row=%u x=%u pixels=%u data=%s\n",
				(unsigned int)y,
				(unsigned int)x,
				(unsigned int)pixelsThisChunk,
				data);
		}
	}

	XBLF("STEFX: renderer screenshot log end label='%s' out=%ux%u\n",
		label,
		(unsigned int)outWidth,
		(unsigned int)outHeight);
}

static void FakeGL_LogBackbufferImage(D3DSurface *backBuffer, DWORD width, DWORD height, DWORD pitch)
{
	FakeGL_LogSurfaceImage("direct", (const BYTE *)backBuffer->Data, (D3DFORMAT)backBuffer->Format, width, height, pitch);
}

static bool FakeGL_TryXGWriteSurface(D3DSurface *surface)
{
	int i;
	const char *paths[] = {
		"D:\\" STEFX_SCREENSHOT_XGSHOT_FILE,
		"E:\\" STEFX_SCREENSHOT_XGSHOT_FILE,
		"T:\\" STEFX_SCREENSHOT_XGSHOT_FILE,
		STEFX_SCREENSHOT_XGSHOT_FILE,
		NULL
	};

	for (i = 0; paths[i]; ++i)
	{
		DeleteFileA(paths[i]);
		HRESULT hr = XGWriteSurfaceToFile(surface, paths[i]);
		DWORD attrs = GetFileAttributesA(paths[i]);
		XBLF("STEFX: renderer screenshot XGWrite path='%s' hr=0x%08lx exists=%d\n",
			paths[i],
			(unsigned long)hr,
			(attrs != 0xffffffffu) ? 1 : 0);
		if (SUCCEEDED(hr) && attrs != 0xffffffffu)
		{
			return true;
		}
	}

	return false;
}

static bool FakeGL_TryXGWriteFrontBuffer(D3DDevice *device)
{
	D3DSurface *frontBuffer = NULL;
	HRESULT hr;
	bool ok = false;

	if (!device)
	{
		return false;
	}

	hr = device->GetBackBuffer(-1, D3DBACKBUFFER_TYPE_MONO, &frontBuffer);
	XBLF("STEFX: renderer screenshot frontbuffer GetBackBuffer hr=0x%08lx surf=%p",
		(unsigned long)hr,
		frontBuffer);
	if (FAILED(hr) || !frontBuffer)
	{
		return false;
	}

	device->BlockUntilIdle();
	ok = FakeGL_TryXGWriteSurface(frontBuffer);
	frontBuffer->Release();
	return ok;
}

static bool FakeGL_TryXGWriteRenderTarget(D3DDevice *device, const char *label)
{
	D3DSurface *renderTarget = NULL;
	HRESULT hr;
	bool ok = false;
	static int s_renderTargetLogBudget = 12;

	if (!device)
	{
		return false;
	}

	hr = device->GetRenderTarget(&renderTarget);
	if (s_renderTargetLogBudget > 0)
	{
		XBLF("STEFX: renderer screenshot render target GetRenderTarget label='%s' hr=0x%08lx surf=%p",
			label ? label : "unknown",
			(unsigned long)hr,
			renderTarget);
		--s_renderTargetLogBudget;
	}
	if (FAILED(hr) || !renderTarget)
	{
		return false;
	}

	device->BlockUntilIdle();
	ok = FakeGL_TryXGWriteSurface(renderTarget);
	if (ok)
	{
		XBLF("STEFX: renderer screenshot render target XGWrite succeeded label='%s'",
			label ? label : "unknown");
	}
	renderTarget->Release();
	return ok;
}

static HANDLE s_fakeglScreenshotFile = INVALID_HANDLE_VALUE;
static bool s_fakeglScreenshotFileIsNt = false;
static char s_fakeglScreenshotFilePath[128];

static bool FakeGL_OpenScreenshotFile(const char *outputPath, const char *ntOutputPath, HANDLE *out, bool *isNt)
{
	*out = CreateFileA(outputPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (*out != INVALID_HANDLE_VALUE)
	{
		*isNt = false;
		return true;
	}

	DWORD createError = GetLastError();
	long status = -1;
	if (ntOutputPath)
	{
		status = FakeGL_NtCreateOverwrite(ntOutputPath, out);
	}
	if (ntOutputPath && status >= 0 && *out != INVALID_HANDLE_VALUE)
	{
		*isNt = true;
		return true;
	}

	XBLF("STEFX: renderer screenshot open failed d='%s' err=%lu nt='%s' status=0x%08lx",
		outputPath,
		createError,
		ntOutputPath ? ntOutputPath : "",
		status);
		*out = INVALID_HANDLE_VALUE;
	*isNt = false;
	return false;
}

static void FakeGL_CopyPath(char *dest, DWORD destSize, const char *src)
{
	if (!dest || destSize == 0)
	{
		return;
	}
	if (!src)
	{
		src = "";
	}
	strncpy(dest, src, destSize - 1);
	dest[destSize - 1] = '\0';
}

static bool FakeGL_OpenScreenshotFileOnRuntimeDrive(const char *filename, HANDLE *out, bool *isNt, char *openedPath, DWORD openedPathSize)
{
	char path[128];
	char ntPath[160];
	static const char drives[] = { 'D', 'E', 'T' };
	int i;

	for (i = 0; i < (int)(sizeof(drives) / sizeof(drives[0])); ++i)
	{
		sprintf(path, "%c:\\%s", drives[i], filename);
		if (FakeGL_OpenScreenshotFile(path, NULL, out, isNt))
		{
			FakeGL_CopyPath(openedPath, openedPathSize, path);
			return true;
		}
	}

	if (FakeGL_OpenScreenshotFile(filename, NULL, out, isNt))
	{
		FakeGL_CopyPath(openedPath, openedPathSize, filename);
		return true;
	}

	sprintf(ntPath, "\\Device\\Harddisk0\\Partition1\\%s", filename);
	*out = INVALID_HANDLE_VALUE;
	long status = FakeGL_NtCreateOverwrite(ntPath, out);
	if (status >= 0 && *out != INVALID_HANDLE_VALUE)
	{
		*isNt = true;
		FakeGL_CopyPath(openedPath, openedPathSize, ntPath);
		XBLF("STEFX: renderer screenshot opened raw partition path='%s'", ntPath);
		return true;
	}

	*isNt = false;
	XBLF("STEFX: renderer screenshot open failed on runtime drives file='%s' nt='%s' status=0x%08lx",
		filename,
		ntPath,
		status);
	return false;
}

static void FakeGL_EnsureScreenshotFilePreopened(void)
{
	static bool s_preopenTried = false;

	if (s_preopenTried || s_fakeglScreenshotFile != INVALID_HANDLE_VALUE)
	{
		return;
	}

	if (!FakeGL_FileExistsOnAnyRuntimeDrive(STEFX_SCREENSHOT_PREOPEN_FILE))
	{
		return;
	}

	s_preopenTried = true;
	if (!FakeGL_OpenScreenshotFileOnRuntimeDrive(STEFX_SCREENSHOT_BACKBUFFER_FILE,
		&s_fakeglScreenshotFile,
		&s_fakeglScreenshotFileIsNt,
		s_fakeglScreenshotFilePath,
		sizeof(s_fakeglScreenshotFilePath)))
	{
		XBLF("STEFX: renderer screenshot preopen failed");
	}
	else
	{
		XBLF("STEFX: renderer screenshot preopened '%s' isNt=%d", s_fakeglScreenshotFilePath, s_fakeglScreenshotFileIsNt ? 1 : 0);
	}
}

static void FakeGL_TryWriteRequestedBackbufferBMP(D3DDevice *device, D3DSurface *backBuffer, DWORD width, DWORD height, DWORD pitch, const char *label)
{
	const char *requestPath = FakeGL_FindScreenshotRequestPath();
	char outputPath[128];
	static int s_rejectLogBudget = 4;
	static int s_blankRetryLogBudget = 4;
	static int s_requestSeenLogBudget = 4;
	DWORD visibleSamples = 0;
	DWORD maxBrightness = 0;

	if (!requestPath)
	{
		return;
	}

	if (s_requestSeenLogBudget > 0)
	{
		XBLF("STEFX: renderer screenshot request seen path='%s' label='%s' size=%ux%u pitch=%u",
			requestPath,
			label,
			(unsigned int)width,
			(unsigned int)height,
			(unsigned int)pitch);
		--s_requestSeenLogBudget;
	}

#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_MP)
	{
		static int s_renderTargetLogBudget = 4;
		if (s_renderTargetLogBudget > 0)
		{
			XBLF("STEFX_HM: renderer screenshot render-target XGWrite enabled for MP visual proof label='%s'",
				label ? label : "unknown");
			--s_renderTargetLogBudget;
		}
	}
#endif
	if (FakeGL_TryXGWriteRenderTarget(device, label))
	{
		FakeGL_DeleteScreenshotRequests();
		return;
	}

	if (!backBuffer || !backBuffer->Data || width == 0 || height == 0 || width > 4096 || height > 4096)
	{
		if (s_rejectLogBudget > 0)
		{
			XBLF("STEFX: renderer screenshot invalid label='%s' surf=%p data=%p size=%ux%u pitch=%u",
				label,
				backBuffer,
				backBuffer ? backBuffer->Data : NULL,
				(unsigned int)width,
				(unsigned int)height,
				(unsigned int)pitch);
			--s_rejectLogBudget;
		}
		FakeGL_DeleteScreenshotRequests();
		return;
	}

	const bool rgb565 =
		backBuffer->Format == D3DFMT_R5G6B5 ||
		backBuffer->Format == D3DFMT_LIN_R5G6B5;
	const DWORD bytesPerPixel = rgb565 ? 2 : 4;
	if (pitch < width * bytesPerPixel)
	{
		if (s_rejectLogBudget > 0)
		{
			XBLF("STEFX: renderer screenshot bad pitch label='%s' pitch=%u width=%u bpp=%u fmt=0x%08x",
				label,
				(unsigned int)pitch,
				(unsigned int)width,
				(unsigned int)bytesPerPixel,
				(unsigned int)backBuffer->Format);
			--s_rejectLogBudget;
		}
		FakeGL_DeleteScreenshotRequests();
		return;
	}

	if (!FakeGL_SurfaceHasVisibleSignal((const BYTE *)backBuffer->Data, (D3DFORMAT)backBuffer->Format, width, height, pitch, &visibleSamples, &maxBrightness))
	{
		const bool postPresent = !strcmp(label, "post-present");
		const bool prePresent = !strcmp(label, "pre-present");
		if (prePresent)
		{
			XBLF("STEFX: renderer screenshot pre-present blank; deferring capture until post-present label='%s'", label);
			return;
		}
		if (s_blankRetryLogBudget > 0)
		{
			XBLF("STEFX: renderer screenshot blank label='%s' visibleSamples=%u maxBrightness=%u size=%ux%u pitch=%u fmt=0x%08x",
				label,
				(unsigned int)visibleSamples,
				(unsigned int)maxBrightness,
				(unsigned int)width,
				(unsigned int)height,
				(unsigned int)pitch,
				(unsigned int)backBuffer->Format);
			--s_blankRetryLogBudget;
		}
		if (postPresent && FakeGL_TryXGWriteFrontBuffer(device))
		{
			XBLF("STEFX: renderer screenshot frontbuffer XGWrite succeeded after blank raw surface label='%s'", label);
			FakeGL_DeleteScreenshotRequests();
			return;
		}
		if (FakeGL_TryXGWriteSurface(backBuffer))
		{
			XBLF("STEFX: renderer screenshot XGWrite succeeded after blank raw surface label='%s'", label);
			FakeGL_DeleteScreenshotRequests();
			return;
		}
		if (postPresent)
		{
			XBLF("STEFX: renderer screenshot retry blank label='%s'; keeping request for next present", label);
		}
		return;
	}

	const DWORD rowStride = (width * 3 + 3) & ~3u;
	const DWORD imageBytes = rowStride * height;
	FakeGL_LogSurfaceSample(label, (const BYTE *)backBuffer->Data, (D3DFORMAT)backBuffer->Format, width, height, pitch);
	FakeGL_LogBackbufferImage(backBuffer, width, height, pitch);

#ifdef _XBOX
	{
		static int s_skipResolveLogBudget = 4;
		if (s_skipResolveLogBudget > 0)
		{
			XBLF("STEFX: renderer screenshot resolve path disabled label='%s'; using direct/log capture",
				label);
			--s_skipResolveLogBudget;
		}
	}
#endif

	BYTE *row = (BYTE *)malloc(rowStride);
	if (!row)
	{
		XBLF("STEFX: renderer screenshot row alloc failed stride=%u", (unsigned int)rowStride);
		FakeGL_LogSurfaceSample(label, (const BYTE *)backBuffer->Data, (D3DFORMAT)backBuffer->Format, width, height, pitch);
		FakeGL_LogBackbufferImage(backBuffer, width, height, pitch);
		return;
	}

	HANDLE file = s_fakeglScreenshotFile;
	bool fileIsNt = s_fakeglScreenshotFileIsNt;
	FakeGL_CopyPath(outputPath, sizeof(outputPath), s_fakeglScreenshotFilePath);
	if (file == INVALID_HANDLE_VALUE)
	{
		if (!FakeGL_OpenScreenshotFileOnRuntimeDrive(STEFX_SCREENSHOT_BACKBUFFER_FILE,
			&file,
			&fileIsNt,
			outputPath,
			sizeof(outputPath)))
		{
			FakeGL_LogBackbufferImage(backBuffer, width, height, pitch);
			free(row);
			return;
		}
	}
	s_fakeglScreenshotFile = INVALID_HANDLE_VALUE;
	s_fakeglScreenshotFileIsNt = false;
	s_fakeglScreenshotFilePath[0] = '\0';

	BYTE header[54];
	memset(header, 0, sizeof(header));
	header[0] = 'B';
	header[1] = 'M';
	FakeGL_WriteLe32(header + 2, 54 + imageBytes);
	FakeGL_WriteLe32(header + 10, 54);
	FakeGL_WriteLe32(header + 14, 40);
	FakeGL_WriteLe32(header + 18, width);
	FakeGL_WriteLe32(header + 22, height);
	FakeGL_WriteLe16(header + 26, 1);
	FakeGL_WriteLe16(header + 28, 24);
	FakeGL_WriteLe32(header + 34, imageBytes);

	bool ok = fileIsNt ? FakeGL_NtWriteAll(file, header, sizeof(header)) : FakeGL_WriteAll(file, header, sizeof(header));
	int y;
	for (y = (int)height - 1; ok && y >= 0; --y)
	{
		memset(row, 0, rowStride);
		const BYTE *src = (const BYTE *)backBuffer->Data + (DWORD)y * pitch;
		for (DWORD x = 0; x < width; ++x)
		{
			if (rgb565)
			{
				WORD pixel = *(const WORD *)(src + x * 2);
				BYTE r = (BYTE)((((pixel >> 11) & 31) * 255) / 31);
				BYTE g = (BYTE)((((pixel >> 5) & 63) * 255) / 63);
				BYTE b = (BYTE)(((pixel & 31) * 255) / 31);
				row[x * 3 + 0] = b;
				row[x * 3 + 1] = g;
				row[x * 3 + 2] = r;
			}
			else
			{
				const BYTE *pixel = src + x * 4;
				row[x * 3 + 0] = pixel[0];
				row[x * 3 + 1] = pixel[1];
				row[x * 3 + 2] = pixel[2];
			}
		}
		ok = fileIsNt ? FakeGL_NtWriteAll(file, row, rowStride) : FakeGL_WriteAll(file, row, rowStride);
	}

	if (fileIsNt)
	{
		FGL_IOSB flushIosb;
		NtFlushBuffersFile(file, &flushIosb);
		NtClose(file);
	}
	else
	{
		FlushFileBuffers(file);
		CloseHandle(file);
	}
	free(row);

	if (ok)
	{
		XBLF("STEFX: renderer screenshot wrote '%s' size=%ux%u pitch=%u fmt=0x%08x",
			outputPath,
			(unsigned int)width,
			(unsigned int)height,
			(unsigned int)pitch,
			(unsigned int)backBuffer->Format);
		FakeGL_DeleteScreenshotRequests();
	}
	else
	{
		XBLF("STEFX: renderer screenshot write failed '%s'", outputPath);
		FakeGL_LogBackbufferImage(backBuffer, width, height, pitch);
		DeleteFileA(outputPath);
		FakeGL_DeleteScreenshotRequests();
	}
}

static bool FakeGL_RenderProbeRequested(void)
{
	return FakeGL_FileExistsOnAnyRuntimeDrive(STEFX_RENDERPROBE_FILE);
}

static int FakeGL_STEFXMatrixLoadMode(void)
{
	static int s_mode = -1;
	if (s_mode >= 0)
	{
		return s_mode;
	}

	if (FakeGL_FileExistsOnAnyRuntimeDrive("ef_mp_matrix_raw.txt"))
	{
		s_mode = 0;
	}
	else if (FakeGL_FileExistsOnAnyRuntimeDrive("ef_mp_matrix_proj.txt"))
	{
		s_mode = 1;
	}
	else if (FakeGL_FileExistsOnAnyRuntimeDrive("ef_mp_matrix_model.txt"))
	{
		s_mode = 2;
	}
	else if (FakeGL_FileExistsOnAnyRuntimeDrive("ef_mp_matrix_both.txt"))
	{
		s_mode = 3;
	}
	else
	{
		s_mode = 0;
	}

	XBLF("STEFX: fakegl matrix load mode=%d raw=0 proj=1 model=2 both=3", s_mode);
	return s_mode;
}

static bool FakeGL_STEFXShouldTransposeMatrix(GLenum matrixMode)
{
	const int mode = FakeGL_STEFXMatrixLoadMode();
	if (mode == 1)
	{
		return matrixMode == GL_PROJECTION;
	}
	if (mode == 2)
	{
		return matrixMode == GL_MODELVIEW;
	}
	if (mode == 3)
	{
		return true;
	}
	return false;
}

static void FakeGL_DrawRenderProbe(D3DDevice *device)
{
	static int s_probeLogCount = 0;
	if (!device || !FakeGL_RenderProbeRequested())
	{
		return;
	}

	struct probeVertex_t
	{
		float x;
		float y;
		float z;
		float rhw;
		DWORD color;
	};

	const probeVertex_t verts[4] =
	{
		{  64.0f,  64.0f, 0.0f, 1.0f, 0xffff0000 },
		{ 576.0f,  64.0f, 0.0f, 1.0f, 0xff00ff00 },
		{  64.0f, 416.0f, 0.0f, 1.0f, 0xff0000ff },
		{ 576.0f, 416.0f, 0.0f, 1.0f, 0xffffffff },
	};

	HRESULT hrClear = device->Clear(0, NULL, D3DCLEAR_TARGET, 0xff400040, 1.0f, 0);
	device->SetTexture(0, NULL);
	device->SetTexture(1, NULL);
	device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
	device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
	device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
	device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	HRESULT hrShader = device->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
	HRESULT hrDraw = device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, verts, sizeof(verts[0]));
	device->KickPushBuffer();
	device->BlockUntilIdle();

	if (s_probeLogCount < 64)
	{
		XBLF("STEFX: fakegl render probe frame=%d clear=0x%08lx shader=0x%08lx draw=0x%08lx flushed=1 dev=%p",
			s_probeLogCount,
			(unsigned long)hrClear,
			(unsigned long)hrShader,
			(unsigned long)hrDraw,
			device);
		++s_probeLogCount;
	}
}
#endif

class TextureEntry
{
public:
	TextureEntry()
	{
		m_id = 0;
		m_mipMap = 0;
		m_format = D3DFMT_UNKNOWN;
		m_internalFormat = 0;
#ifdef _XBOX
		m_ownsTextureHeader = false;
#endif

		m_glTexParameter2DMinFilter = GL_LINEAR_MIPMAP_LINEAR;	// Set up trilinear filtering
		m_glTexParameter2DMagFilter = GL_LINEAR;
		m_glTexParameter2DWrapS = GL_CLAMP;						// FakeGL 2009 sets it to WRAP -> CLAMP
		m_glTexParameter2DWrapT = GL_CLAMP;
		m_maxAnisotropy = 4.0;									// We also can bump up the anisotropy level to make things look nicer
	}
	~TextureEntry()
	{
	}

	void Release()
	{
#ifdef _XBOX
		if (m_mipMap && m_ownsTextureHeader)
		{
			delete (D3DTexture*)m_mipMap;
			m_mipMap = 0;
			m_ownsTextureHeader = false;
			return;
		}
#endif
		RELEASENULL(m_mipMap);
		m_mipMap = 0;
#ifdef _XBOX
		m_ownsTextureHeader = false;
#endif
	}

	GLuint m_id;
	IDirect3DTexture8* m_mipMap;
	D3DFORMAT m_format;
	GLint m_internalFormat;
#ifdef _XBOX
	bool m_ownsTextureHeader;
#endif

	GLint m_glTexParameter2DMinFilter;
	GLint m_glTexParameter2DMagFilter;
	GLint m_glTexParameter2DWrapS;
	GLint m_glTexParameter2DWrapT;
	float m_maxAnisotropy;
};


#define TASIZE 2000

class TextureTable 
{
public:
	TextureTable()
	{
		m_count = 0;
		m_size = 0;
		m_textures = 0;
		m_currentTexture = 0;
		m_currentID = 0;
		BindTexture(0);
	}
	~TextureTable()
	{
		DWORD i;
		for(i = 0; i < m_count; i++) 
		{
			m_textures[i].Release();

		}
		for(i = 0; i < TASIZE; i++) 
		{
			m_textureArray[i].Release();
		}

		delete [] m_textures;
	}

	void BindTexture(GLuint id)
	{
		TextureEntry* oldEntry = m_currentTexture;
		m_currentID = id;

		if ( id < TASIZE )
		{
			m_currentTexture = m_textureArray + id;
			if ( m_currentTexture->m_id )
			{
				return;
			}
		}
		else 
		{
			// Check overflow table.
			// Really ought to be a hash table.
			for(DWORD i = 0; i < m_count; i++)
			{
				if ( id == m_textures[i].m_id ) 
				{
					m_currentTexture =  m_textures + i;
					return;
				}
			}
			// It's a new ID.
			// Ensure space in the table
			if ( m_count >= m_size ) 
			{
				int newSize = m_size * 2 + 10;
				TextureEntry* newTextures = new TextureEntry[newSize];
				for(DWORD i = 0; i < m_count; i++ ) 
				{
					newTextures[i] = m_textures[i];
				}
				delete[] m_textures;
				m_textures = newTextures;
				m_size = newSize;
			}
			// Put new entry in table
			oldEntry = m_currentTexture;
			m_currentTexture = m_textures + m_count;
			m_count++;
		}
		if ( oldEntry ) 
		{
			*m_currentTexture = *oldEntry;
		}
		m_currentTexture->m_id = id;
		m_currentTexture->m_mipMap = NULL;		
	}

	int GetCurrentID() 
	{
		return m_currentID;
	}

	TextureEntry* GetCurrentEntry() 
	{
		return m_currentTexture;
	}

	TextureEntry* GetEntry(GLuint id)
	{
		if ( m_currentID == id && m_currentTexture ) 
		{
			return m_currentTexture;
		}
		if ( id < TASIZE ) 
		{
			return &m_textureArray[id];
		}
		else 
		{
			// Check overflow table.
			// Really ought to be a hash table.
			for(DWORD i = 0; i < m_count; i++)
			{
				if ( id == m_textures[i].m_id )
				{
					return  &m_textures[i];
				}
			}
		}
		return 0;
	}

	IDirect3DTexture8*  GetMipMap()
	{
		if ( m_currentTexture )
		{
			return m_currentTexture->m_mipMap;
		}
		return 0;
	}

	IDirect3DTexture8*  GetMipMap(int id)
	{
		TextureEntry* entry = GetEntry(id);
		if ( entry ) 
		{
			return entry->m_mipMap;
		}
		return 0;
	}

	D3DFORMAT GetSurfaceFormat()
	{
		if ( m_currentTexture ) 
		{
			return m_currentTexture->m_format;
		}
		return D3DFMT_UNKNOWN;
	}

	void SetTexture(IDirect3DTexture8* mipMap, D3DFORMAT d3dFormat, GLint internalFormat
#ifdef _XBOX
		, bool ownsTextureHeader = false
#endif
		)
	{
		if ( !m_currentTexture )
		{
			BindTexture(0);
		}
		m_currentTexture->Release();
		m_currentTexture->m_mipMap = mipMap;
		m_currentTexture->m_format = d3dFormat;
		m_currentTexture->m_internalFormat = internalFormat;
#ifdef _XBOX
		m_currentTexture->m_ownsTextureHeader = ownsTextureHeader;
#endif
	}

	void DeleteTexture(GLuint id)
	{
		TextureEntry* entry = GetEntry(id);
		if (!entry)
			return;
		entry->Release();
		entry->m_id = 0;
		entry->m_mipMap = NULL;
		entry->m_format = D3DFMT_UNKNOWN;
		entry->m_internalFormat = 0;
#ifdef _XBOX
		entry->m_ownsTextureHeader = false;
#endif
		if (m_currentID == id)
			BindTexture(0);
	}

	bool IsTexture(GLuint id)
	{
		if (!id)
			return false;
		TextureEntry* entry = GetEntry(id);
		return entry && entry->m_id == id;
	}

	GLint GetInternalFormat() 
	{
		if ( m_currentTexture ) 
		{
			return m_currentTexture->m_internalFormat;
		}
		return 0;
	}
private:
	GLuint m_currentID;
	DWORD m_count;
	DWORD m_size;
	TextureEntry m_textureArray[TASIZE];	// IDs 0..TASIZE-1
	TextureEntry* m_textures;				// Overflow

	TextureEntry* m_currentTexture;
};

#if 1
#define Clamp(x) (x) // No clamping -- we've made sure the inputs are in the range 0..1
#else
float Clamp(float x) 
{
	if ( x < 0 ) 
	{
		x = 0;
		LocalDebugBreak();
	}
	else if ( x > 1 ) 
	{
		x = 1;
		LocalDebugBreak();
	}
	return x;
}
#endif

static D3DBLEND GLToDXSBlend(GLenum glBlend)
{
	D3DBLEND result = D3DBLEND_ONE;
	switch ( glBlend ) 
	{
		case GL_ZERO: result = D3DBLEND_ZERO; break;
		case GL_ONE: result = D3DBLEND_ONE; break;
		case GL_DST_COLOR: result = D3DBLEND_DESTCOLOR; break;
		case GL_ONE_MINUS_DST_COLOR: result = D3DBLEND_INVDESTCOLOR; break;
		case GL_SRC_ALPHA: result = D3DBLEND_SRCALPHA; break;
		case GL_ONE_MINUS_SRC_ALPHA: result = D3DBLEND_INVSRCALPHA; break;
		case GL_DST_ALPHA: result = D3DBLEND_DESTALPHA; break;
		case GL_ONE_MINUS_DST_ALPHA: result = D3DBLEND_INVDESTALPHA; break;
		case GL_SRC_ALPHA_SATURATE: result = D3DBLEND_SRCALPHASAT; break;
		default: LocalDebugBreak(); break;
	}
	return result;
}

static D3DBLEND GLToDXDBlend(GLenum glBlend)
{
	D3DBLEND result = D3DBLEND_ONE;
	switch ( glBlend )
	{
		case GL_ZERO: result = D3DBLEND_ZERO; break;
		case GL_ONE: result = D3DBLEND_ONE; break;
		case GL_SRC_COLOR: result = D3DBLEND_SRCCOLOR; break;
		case GL_ONE_MINUS_SRC_COLOR: result = D3DBLEND_INVSRCCOLOR; break;
		case GL_SRC_ALPHA: result = D3DBLEND_SRCALPHA; break;
		case GL_ONE_MINUS_SRC_ALPHA: result = D3DBLEND_INVSRCALPHA; break;
		case GL_DST_ALPHA: result = D3DBLEND_DESTALPHA; break;
		case GL_ONE_MINUS_DST_ALPHA: result = D3DBLEND_INVDESTALPHA; break;
		default: LocalDebugBreak(); break;
	}
	return result;
}

static D3DCMPFUNC GLToDXCompare(GLenum func)
{
	D3DCMPFUNC result = D3DCMP_ALWAYS;
	switch ( func ) 
	{
		case GL_NEVER: result = D3DCMP_NEVER; break;
		case GL_LESS: result = D3DCMP_LESS; break;
		case GL_EQUAL: result = D3DCMP_EQUAL; break;
		case GL_LEQUAL: result = D3DCMP_LESSEQUAL; break;
		case GL_GREATER: result = D3DCMP_GREATER; break;
		case GL_NOTEQUAL: result = D3DCMP_NOTEQUAL; break;
		case GL_GEQUAL: result = D3DCMP_GREATEREQUAL; break;
		case GL_ALWAYS: result = D3DCMP_ALWAYS; break;
		default: break;
	}
	return result;
}

static D3DFOGMODE GLToDXFogMode(GLint mode)
{
	switch ( mode )
	{
	case GL_LINEAR:
		return D3DFOG_LINEAR;
	case GL_EXP:
		return D3DFOG_EXP;
	case GL_EXP2:
		return D3DFOG_EXP2;
	default:
		break;
	}
	return D3DFOG_NONE;
}

/*
   OpenGL                      MinFilter           MipFilter       Comments
   GL_NEAREST                  D3DTFN_POINT        D3DTFP_NONE
   GL_LINEAR                   D3DTFN_LINEAR       D3DTFP_NONE
   GL_NEAREST_MIPMAP_NEAREST   D3DTFN_POINT        D3DTFP_POINT        
   GL_LINEAR_MIPMAP_NEAREST    D3DTFN_LINEAR       D3DTFP_POINT    bilinear
   GL_NEAREST_MIPMAP_LINEAR    D3DTFN_POINT        D3DTFP_LINEAR 
   GL_LINEAR_MIPMAP_LINEAR     D3DTFN_LINEAR       D3DTFP_LINEAR   trilinear
*/

static D3DTEXTUREFILTERTYPE GLToDXMinFilter(GLint filter)
{
	D3DTEXTUREFILTERTYPE result = D3DTEXF_LINEAR;
	switch ( filter ) 
	{
		case GL_NEAREST: result = D3DTEXF_POINT; break;
		case GL_LINEAR: result = D3DTEXF_LINEAR; break;
		case GL_NEAREST_MIPMAP_NEAREST: result = D3DTEXF_POINT; break;
		case GL_LINEAR_MIPMAP_NEAREST: result = D3DTEXF_LINEAR; break;
		case GL_NEAREST_MIPMAP_LINEAR: result = D3DTEXF_POINT; break;
		case GL_LINEAR_MIPMAP_LINEAR: result = D3DTEXF_LINEAR; break;
	default:
		LocalDebugBreak();
		break;
	}
	return result;
}

static D3DTEXTUREFILTERTYPE GLToDXMipFilter(GLint filter)
{
	D3DTEXTUREFILTERTYPE result = D3DTEXF_LINEAR;
	switch ( filter ) 
	{
		case GL_NEAREST: result = D3DTEXF_NONE; break;
		case GL_LINEAR: result = D3DTEXF_NONE; break;
		case GL_NEAREST_MIPMAP_NEAREST: result = D3DTEXF_POINT; break;
		case GL_LINEAR_MIPMAP_NEAREST: result = D3DTEXF_POINT; break;
		case GL_NEAREST_MIPMAP_LINEAR: result = D3DTEXF_LINEAR; break;
		case GL_LINEAR_MIPMAP_LINEAR: result = D3DTEXF_LINEAR; break;
	default:
		LocalDebugBreak();
		break;
	}
	return result;
}

static D3DTEXTUREFILTERTYPE GLToDXMagFilter(GLint filter)
{
	D3DTEXTUREFILTERTYPE result = D3DTEXF_POINT;
	switch ( filter )
	{
		case GL_NEAREST: result = D3DTEXF_POINT; break;
		case GL_LINEAR: result = D3DTEXF_LINEAR; break;
	default:
		LocalDebugBreak();
		break;
	}
	return result;
}

static D3DTEXTUREADDRESS GLToDXTextureAddress(GLint wrap)
{
	D3DTEXTUREADDRESS result = D3DTADDRESS_CLAMP;
	switch ( wrap )
	{
		case GL_REPEAT:
			result = D3DTADDRESS_WRAP;
			break;
		case GL_CLAMP:
#ifdef GL_CLAMP_TO_EDGE
		case GL_CLAMP_TO_EDGE:
#endif
			result = D3DTADDRESS_CLAMP;
			break;
		default:
			LocalDebugBreak();
			break;
	}
	return result;
}

static D3DTEXTUREOP GLToDXTextEnvMode(GLint mode)
{
	D3DTEXTUREOP result = D3DTOP_MODULATE;
	switch ( mode ) 
	{
		case GL_MODULATE: result = D3DTOP_MODULATE; break;
		case GL_DECAL: result = D3DTOP_SELECTARG1; break; // Fix this
		case GL_BLEND: result = D3DTOP_BLENDTEXTUREALPHA; break;
		case GL_REPLACE: result = D3DTOP_SELECTARG1; break;
		case GL_ADD: result = D3DTOP_ADD; break;
		default: break;
	}
	return result;
}

#define MAXSTATES 8

class TextureStageState 
{
public:
	TextureStageState()
	{
		m_currentTexture = 0;
		m_glTextEnvMode = GL_MODULATE;
		m_glTexture2D = false;
		m_dirty = true;
	}

	bool GetDirty()
	{
		return m_dirty;
	}

	void SetDirty(bool dirty) 
	{ 
		m_dirty = dirty;
	}

	bool DirtyTexture(GLuint textureID)
	{
		if ( textureID == m_currentTexture ) 
		{
			m_dirty = true;
			return true;
		}
		return false;
	}

	GLuint GetCurrentTexture() { return m_currentTexture; }
	bool SetCurrentTexture(GLuint texture)
	{
		if (m_currentTexture == texture)
		{
			return false;
		}
		m_dirty = true;
		m_currentTexture = texture;
		return true;
	}

	GLfloat GetTextEnvMode() { return m_glTextEnvMode; }
	bool SetTextEnvMode(GLfloat mode)
	{
		if (m_glTextEnvMode == mode)
		{
			return false;
		}
		m_dirty = true;
		m_glTextEnvMode = mode;
		return true;
	}

	bool GetTexture2D() { return m_glTexture2D; }
	bool SetTexture2D(bool texture2D)
	{
		if (m_glTexture2D == texture2D)
		{
			return false;
		}
		m_dirty = true;
		m_glTexture2D = texture2D;
		return true;
	}

private:
	
	GLuint m_currentTexture;
	GLfloat m_glTextEnvMode;
	bool m_glTexture2D;
	bool m_dirty;
};

class TextureState
{
public:
	TextureState()
	{
		m_currentStage = 0;
		memset(&m_stage, 0, sizeof(m_stage));
		m_dirty = false;
		m_mainBlend = false;
	}

	void SetMaxStages(int maxStages)
	{
		m_maxStages = maxStages;
		for(int i = 0; i < m_maxStages;i++)
		{
			m_stage[i].SetDirty(true);
		}
		m_dirty = true;
	}

	// Keep track of changes to texture stage state
	void SetCurrentStage(int index)
	{
		m_currentStage = index;
	}

	int GetMaxStages() { return m_maxStages; }
	int GetCurrentStage() { return m_currentStage; }
	GLuint GetStageTexture(int index) { return (index >= 0 && index < MAXSTATES) ? m_stage[index].GetCurrentTexture() : 0; }
	bool GetStageTexture2D(int index) { return (index >= 0 && index < MAXSTATES) ? m_stage[index].GetTexture2D() : false; }
	bool GetStageDirty(int index) { return (index >= 0 && index < MAXSTATES) ? m_stage[index].GetDirty() : false; }
	GLfloat GetStageTextEnvMode(int index) { return (index >= 0 && index < MAXSTATES) ? m_stage[index].GetTextEnvMode() : 0.0f; }

	bool GetDirty() { return m_dirty; }

	void DirtyTexture(int textureID)
	{
		bool dirty = false;
		for(int i = 0; i < m_maxStages;i++)
		{
			dirty = m_stage[i].DirtyTexture(textureID) || dirty;
		}
		if (dirty)
		{
			m_dirty = true;
		}
	}

	void SetMainBlend(bool mainBlend)
	{
		if (m_mainBlend == mainBlend)
		{
			return;
		}
		m_mainBlend = mainBlend;
		m_stage[0].SetDirty(true);
		m_dirty = true;
	}
	
	// These methods apply to the current stage

	GLuint GetCurrentTexture() { return Get()->GetCurrentTexture(); }

	void SetCurrentTexture(GLuint texture)
	{
		if (Get()->SetCurrentTexture(texture))
		{
			m_dirty = true;
		}
	}

	GLfloat GetTextEnvMode() { return Get()->GetTextEnvMode(); }

	void SetTextEnvMode(GLfloat mode)
	{
		if (Get()->SetTextEnvMode(mode))
		{
			m_dirty = true;
		}
	}
	
	bool GetTexture2D() { return Get()->GetTexture2D(); }

	void SetTexture2D(bool texture2D)
	{
		if (Get()->SetTexture2D(texture2D))
		{
			m_dirty = true;
		}
	}

	void ForceStageDirty(int index)
	{
		if (index >= 0 && index < m_maxStages)
		{
			m_stage[index].SetDirty(true);
			m_dirty = true;
		}
	}

	void SetTextureStageState(IDirect3DDevice8* pD3DDev, TextureTable* textures)
	{
#ifdef _XBOX
		{
			static int s_xboxTextureStateEntryStage1LogCount = 0;
			if (m_stage[1].GetTexture2D() && s_xboxTextureStateEntryStage1LogCount < 8)
			{
				XBLF("JA: fakegl texture state entry dirty=%d maxStages=%d currentStage=%d stage0 dirty=%d tex=%u enabled=%d env=0x%08x stage1 dirty=%d tex=%u enabled=%d env=0x%08x",
					m_dirty ? 1 : 0,
					m_maxStages,
					m_currentStage,
					m_stage[0].GetDirty() ? 1 : 0,
					(unsigned int)m_stage[0].GetCurrentTexture(),
					m_stage[0].GetTexture2D() ? 1 : 0,
					(unsigned int)m_stage[0].GetTextEnvMode(),
					m_stage[1].GetDirty() ? 1 : 0,
					(unsigned int)m_stage[1].GetCurrentTexture(),
					m_stage[1].GetTexture2D() ? 1 : 0,
					(unsigned int)m_stage[1].GetTextEnvMode());
				++s_xboxTextureStateEntryStage1LogCount;
			}
		}
#endif
		if ( ! m_dirty )
		{
#ifdef _XBOX
			static int s_xboxTextureStateCleanLogCount = 0;
			if (s_xboxTextureStateCleanLogCount < 4)
			{
				XBLF("JA: fakegl texture state apply skipped dirty=0 currentStage=%d maxStages=%d stage0 tex=%u enabled=%d stage1 tex=%u enabled=%d",
					m_currentStage,
					m_maxStages,
					(unsigned int)m_stage[0].GetCurrentTexture(),
					m_stage[0].GetTexture2D() ? 1 : 0,
					(unsigned int)m_stage[1].GetCurrentTexture(),
					m_stage[1].GetTexture2D() ? 1 : 0);
				++s_xboxTextureStateCleanLogCount;
			}
#endif
			return;
		}
		static bool firstTime = true;
		if ( firstTime ) 
		{
			firstTime = false;
			for(int i = 0; i < m_maxStages; i++ ) 
			{
				pD3DDev->SetTextureStageState(i, D3DTSS_TEXCOORDINDEX, i);
				pD3DDev->SetTextureStageState(i, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
			}
		}

		m_dirty = false;

		for(int i = 0; i < m_maxStages; i++ )
		{
#ifdef _XBOX
			{
				static int s_xboxTextureStageLoopLogCount = 0;
				if ((i == 1 || s_xboxTextureStageLoopLogCount < 4) && s_xboxTextureStageLoopLogCount < 16)
				{
					XBLF("JA: fakegl texture stage loop i=%d maxStages=%d dirty=%d tex=%u enabled=%d env=0x%08x textureDirty=%d currentStage=%d",
						i,
						m_maxStages,
						m_stage[i].GetDirty() ? 1 : 0,
						(unsigned int)m_stage[i].GetCurrentTexture(),
						m_stage[i].GetTexture2D() ? 1 : 0,
						(unsigned int)m_stage[i].GetTextEnvMode(),
						m_dirty ? 1 : 0,
						m_currentStage);
					++s_xboxTextureStageLoopLogCount;
				}
			}
#endif
			if ( ! m_stage[i].GetDirty() ) 
			{
#ifdef _XBOX
				static int s_xboxStageCleanLogCount = 0;
				if (i == 1 && s_xboxStageCleanLogCount < 8)
				{
					XBLF("JA: fakegl stage state stage=1 skip dirty=0 tex=%u enabled=%d env=0x%08x currentStage=%d",
						(unsigned int)m_stage[i].GetCurrentTexture(),
						m_stage[i].GetTexture2D() ? 1 : 0,
						(unsigned int)m_stage[i].GetTextEnvMode(),
						m_currentStage);
					++s_xboxStageCleanLogCount;
				}
#endif
				continue;
			}
			m_stage[i].SetDirty(false);

			if ( m_stage[i].GetTexture2D() ) 
			{
				DWORD color1 = D3DTA_TEXTURE;
				int textEnvMode =  m_stage[i].GetTextEnvMode();
				DWORD colorOp = GLToDXTextEnvMode(textEnvMode);
				if ( i > 0 && textEnvMode == GL_BLEND )
				{
					colorOp = D3DTOP_MODULATE;
					color1 |= D3DTA_COMPLEMENT;
				}
#ifdef _XBOX
				if (i == 1)
				{
					static int s_xboxStage1PreApplyLogCount = 0;
					if (s_xboxStage1PreApplyLogCount < 8)
					{
						XBLF("JA: fakegl stage1 preapply tex=%u env=0x%08x colorOp=0x%08lx colorArg1=0x%08lx colorArg2=0x%08lx",
							(unsigned int)m_stage[i].GetCurrentTexture(),
							(unsigned int)textEnvMode,
							(unsigned long)colorOp,
							(unsigned long)color1,
							(unsigned long)D3DTA_CURRENT);
						++s_xboxStage1PreApplyLogCount;
					}
				}
#endif
				HRESULT hrColorArg1 = pD3DDev->SetTextureStageState( i, D3DTSS_COLORARG1, color1);
				HRESULT hrColorArg2 = pD3DDev->SetTextureStageState( i, D3DTSS_COLORARG2, i == 0 ? D3DTA_DIFFUSE : D3DTA_CURRENT);
				HRESULT hrColorOp = pD3DDev->SetTextureStageState( i, D3DTSS_COLOROP, colorOp);
				HRESULT hrTexCoordIndex = pD3DDev->SetTextureStageState( i, D3DTSS_TEXCOORDINDEX, i);
				HRESULT hrTextureTransform = pD3DDev->SetTextureStageState( i, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
				pD3DDev->SetTextureStageState( i, D3DTSS_MAXMIPLEVEL, 0 );
				pD3DDev->SetTextureStageState( i, D3DTSS_MIPMAPLODBIAS, 0 );
				DWORD alpha1 = D3DTA_TEXTURE;
				DWORD alpha2 = D3DTA_DIFFUSE;
				DWORD alphaOp;
				alphaOp = GLToDXTextEnvMode(textEnvMode);
				if (i == 0 && m_mainBlend )
				{
					alphaOp = D3DTOP_MODULATE;	// Otherwise the console is never transparent
				}
				HRESULT hrAlphaArg1 = pD3DDev->SetTextureStageState( i, D3DTSS_ALPHAARG1, alpha1);
				HRESULT hrAlphaArg2 = pD3DDev->SetTextureStageState( i, D3DTSS_ALPHAARG2, alpha2);
				HRESULT hrAlphaOp = pD3DDev->SetTextureStageState( i, D3DTSS_ALPHAOP,   alphaOp);
#ifdef _XBOX
				{
					static int s_xboxStageStateLogCount = 0;
					static int s_xboxStage1StateLogCount = 0;
					if (s_xboxStageStateLogCount < 8 || (i > 0 && s_xboxStage1StateLogCount < 8))
					{
						XBLF("JA: fakegl stage state stage=%d enabled=1 tex=%u env=0x%08x colorOp=0x%08lx colorArg1=0x%08lx colorArg2=0x%08lx alphaOp=0x%08lx alphaArg1=0x%08lx alphaArg2=0x%08lx",
							i,
							(unsigned int)m_stage[i].GetCurrentTexture(),
							(unsigned int)textEnvMode,
							(unsigned long)colorOp,
							(unsigned long)color1,
							(unsigned long)(i == 0 ? D3DTA_DIFFUSE : D3DTA_CURRENT),
							(unsigned long)alphaOp,
							(unsigned long)alpha1,
							(unsigned long)alpha2);
						++s_xboxStageStateLogCount;
						if (i > 0)
						{
							++s_xboxStage1StateLogCount;
						}
					}
				}
				if (i == 1)
				{
					static int s_xboxStage1ApplyHrLogCount = 0;
					if (s_xboxStage1ApplyHrLogCount < 8)
					{
						XBLF("JA: fakegl stage1 apply hr colorArg1=0x%08lx colorArg2=0x%08lx colorOp=0x%08lx alphaArg1=0x%08lx alphaArg2=0x%08lx alphaOp=0x%08lx",
							(unsigned long)hrColorArg1,
							(unsigned long)hrColorArg2,
							(unsigned long)hrColorOp,
							(unsigned long)hrAlphaArg1,
							(unsigned long)hrAlphaArg2,
							(unsigned long)hrAlphaOp);
						++s_xboxStage1ApplyHrLogCount;
					}
				}
				if (i == 1)
				{
					static int s_xboxStage1TexTransformLogCount = 0;
					if (s_xboxStage1TexTransformLogCount < 8)
					{
						XBLF("JA: fakegl stage1 texcoord state hr index=0x%08lx transform=0x%08lx flags=DISABLE",
							(unsigned long)hrTexCoordIndex,
							(unsigned long)hrTextureTransform);
						++s_xboxStage1TexTransformLogCount;
					}
				}
#endif

				TextureEntry* entry = textures->GetEntry(m_stage[i].GetCurrentTexture());
				if ( entry ) 
				{
#ifdef _XBOX
					if (i == 1)
					{
						static int s_xboxStage1EntryLogCount = 0;
						if (s_xboxStage1EntryLogCount < 8)
						{
							XBLF("JA: fakegl stage1 texture entry reqTex=%u entryId=%u ptr=%p fmt=0x%08x internal=0x%08x min=%d mag=%d",
								(unsigned int)m_stage[i].GetCurrentTexture(),
								(unsigned int)entry->m_id,
								(void*)entry->m_mipMap,
								(unsigned int)entry->m_format,
								(unsigned int)entry->m_internalFormat,
								entry->m_glTexParameter2DMinFilter,
								entry->m_glTexParameter2DMagFilter);
							++s_xboxStage1EntryLogCount;
						}
					}
#endif
					int minFilter = entry->m_glTexParameter2DMinFilter;
					DWORD dxMinFilter = GLToDXMinFilter(minFilter);
					DWORD dxMipFilter = GLToDXMipFilter(minFilter);
					DWORD dxMagFilter = GLToDXMagFilter(entry->m_glTexParameter2DMagFilter);
					DWORD dxAddressU = GLToDXTextureAddress(entry->m_glTexParameter2DWrapS);
					DWORD dxAddressV = GLToDXTextureAddress(entry->m_glTexParameter2DWrapT);

					// Avoid setting anisotropic if the user doesn't request it.
					static bool bSetMaxAnisotropy = false;
					if ( entry->m_maxAnisotropy != 1.0f ) 
					{
						bSetMaxAnisotropy = true;
						if ( dxMagFilter == D3DTEXF_LINEAR) 
						{
							dxMagFilter = D3DTEXF_ANISOTROPIC;
						}
						if ( dxMinFilter == D3DTEXF_LINEAR) 
						{
							dxMinFilter = D3DTEXF_ANISOTROPIC;
						}
					}
					if ( bSetMaxAnisotropy ) 
					{
						pD3DDev->SetTextureStageState( i, D3DTSS_MAXANISOTROPY, entry->m_maxAnisotropy);
					}
					pD3DDev->SetTextureStageState( i, D3DTSS_MINFILTER, dxMinFilter );
					pD3DDev->SetTextureStageState( i, D3DTSS_MIPFILTER, dxMipFilter );
					pD3DDev->SetTextureStageState( i, D3DTSS_MAGFILTER,  dxMagFilter);
					pD3DDev->SetTextureStageState( i, D3DTSS_ADDRESSU, dxAddressU );
					pD3DDev->SetTextureStageState( i, D3DTSS_ADDRESSV, dxAddressV );
#ifdef _XBOX
					if (i == 1)
					{
						static int s_xboxStage1AddressLogCount = 0;
						if (s_xboxStage1AddressLogCount < 8)
						{
							XBLF("JA: fakegl stage1 address wrapS=0x%08x wrapT=0x%08x addrU=0x%08lx addrV=0x%08lx",
								(unsigned int)entry->m_glTexParameter2DWrapS,
								(unsigned int)entry->m_glTexParameter2DWrapT,
								(unsigned long)dxAddressU,
								(unsigned long)dxAddressV);
							++s_xboxStage1AddressLogCount;
						}
					}
#endif
					IDirect3DTexture8* pTexture = entry->m_mipMap;
					// char buf[100];
					// sprintf(buf,"SetTexture 0x%08x\n", pTexture);
					// OutputDebugString(buf);
					if ( pTexture )
					{
#ifdef _XBOX
						static int s_xboxSetTextureLogCount = 0;
						static int s_xboxSetTextureStage1LogCount = 0;
						const bool logSetTexture = (s_xboxSetTextureLogCount < 8 || (i > 0 && s_xboxSetTextureStage1LogCount < 8));
						if (i == 1)
						{
							static int s_xboxStage1SetTextureDirectLogCount = 0;
							if (s_xboxStage1SetTextureDirectLogCount < 8)
							{
								XBLF("JA: fakegl stage1 SetTexture direct pre texid=%d ptr=%p fmt=0x%08x internal=0x%08x",
									entry->m_id, (void*)pTexture, (unsigned int)entry->m_format,
									(unsigned int)entry->m_internalFormat);
								++s_xboxStage1SetTextureDirectLogCount;
							}
						}
						if (logSetTexture)
						{
							XBLF("JA: fakegl SetTexture stage=%d texid=%d ptr=%p fmt=0x%08x internal=0x%08x",
								i, entry->m_id, (void*)pTexture, (unsigned int)entry->m_format,
								(unsigned int)entry->m_internalFormat);
						}
#endif
						HRESULT hrSetTexture = pD3DDev->SetTexture( i, pTexture);
#ifdef _XBOX
						{
							static int s_efStage0ApplyBudget = 8;
							static int s_efStage1ApplyBudget = 16;
							bool logStageApply = false;
							if (i == 1 && s_efStage1ApplyBudget > 0)
							{
								logStageApply = true;
								--s_efStage1ApplyBudget;
							}
							else if (i == 0 && s_efStage0ApplyBudget > 0 && entry->m_id > 8)
							{
								logStageApply = true;
								--s_efStage0ApplyBudget;
							}
							if (logStageApply)
							{
								XBLF("EF: TEX_STAGE_APPLY stage=%d texid=%d ptr=%p fmt=0x%08x internal=0x%08x env=0x%08x hr=0x%08lx",
									i,
									entry->m_id,
									(void*)pTexture,
									(unsigned int)entry->m_format,
									(unsigned int)entry->m_internalFormat,
									(unsigned int)m_stage[i].GetTextEnvMode(),
									(unsigned long)hrSetTexture);
							}
						}
						if (i == 1)
						{
							static int s_xboxStage1SetTextureDirectHrLogCount = 0;
							if (s_xboxStage1SetTextureDirectHrLogCount < 8)
							{
								XBLF("JA: fakegl stage1 SetTexture direct post hr=0x%08lx",
									(unsigned long)hrSetTexture);
								++s_xboxStage1SetTextureDirectHrLogCount;
							}
						}
						if (logSetTexture)
						{
							XBLF("JA: fakegl SetTexture stage=%d hr=0x%08lx", i,
								(unsigned long)hrSetTexture);
							++s_xboxSetTextureLogCount;
							if (i > 0)
							{
								++s_xboxSetTextureStage1LogCount;
							}
						}
#endif
					}
					else
					{
#ifdef _XBOX
						if (i == 1)
						{
							static int s_xboxStage1NullTextureLogCount = 0;
							if (s_xboxStage1NullTextureLogCount < 32)
							{
								XBLF("JA: fakegl stage1 texture entry missing mip reqTex=%u entryId=%u",
									(unsigned int)m_stage[i].GetCurrentTexture(),
									(unsigned int)entry->m_id);
								++s_xboxStage1NullTextureLogCount;
							}
						}
#endif
						LocalDebugBreak();
					}
				}
				else
				{
#ifdef _XBOX
					if (i == 1)
					{
						static int s_xboxStage1MissingEntryLogCount = 0;
						if (s_xboxStage1MissingEntryLogCount < 32)
						{
							XBLF("JA: fakegl stage1 texture entry missing reqTex=%u",
								(unsigned int)m_stage[i].GetCurrentTexture());
							++s_xboxStage1MissingEntryLogCount;
						}
					}
#endif
				}
			}
			else 
			{
				pD3DDev->SetTexture( i, NULL);
				pD3DDev->SetTextureStageState( i, D3DTSS_COLORARG1, D3DTA_TEXTURE);
				pD3DDev->SetTextureStageState( i, D3DTSS_COLORARG2, i == 0 ? D3DTA_DIFFUSE : D3DTA_CURRENT);
				pD3DDev->SetTextureStageState( i, D3DTSS_COLOROP, D3DTOP_DISABLE);
				pD3DDev->SetTextureStageState( i, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
				pD3DDev->SetTextureStageState( i, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
				pD3DDev->SetTextureStageState( i, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
				pD3DDev->SetTextureStageState( i, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
				pD3DDev->SetTextureStageState( i, D3DTSS_MAXMIPLEVEL, 0 );
				pD3DDev->SetTextureStageState( i, D3DTSS_MIPMAPLODBIAS, 0 );
#ifdef _XBOX
				pD3DDev->SetTextureStageState( i, D3DTSS_COLORKEYOP, D3DTCOLORKEYOP_DISABLE );
				pD3DDev->SetTextureStageState( i, D3DTSS_COLORSIGN, 0 );
				pD3DDev->SetTextureStageState( i, D3DTSS_ALPHAKILL, D3DTALPHAKILL_DISABLE );
#endif
#ifdef _XBOX
				{
					static int s_xboxStageDisableLogCount = 0;
					if (s_xboxStageDisableLogCount < 8)
					{
						XBLF("JA: fakegl stage state stage=%d enabled=0 tex=%u colorOp=DISABLE",
							i,
							(unsigned int)m_stage[i].GetCurrentTexture());
						++s_xboxStageDisableLogCount;
					}
				}
#endif
			}
		}
	}

private:
	TextureStageState* Get() 
	{
		return m_stage + m_currentStage;
	}

	bool m_dirty;
	bool m_mainBlend;
	int m_maxStages;
	int m_currentStage;
	TextureStageState m_stage[MAXSTATES];
};

// This class buffers up all the glVertex calls between
// glBegin and glEnd.

// USE_DRAWINDEXEDPRIMITIVE seems slightly faster (54 fps vs 53 fps) than USE_DRAWPRIMITIVE.
// USE_DRAWINDEXEDPRIMITIVEVB is much slower (30fps vs 54fps), at least on GeForce Win9x 3.75.

// DrawPrimitive works for DX8, the other ones don't work right yet.

#define USE_DRAWPRIMITIVE

#ifdef USE_DRAWPRIMITIVE
class OGLPrimitiveVertexBuffer 
{
public:
	OGLPrimitiveVertexBuffer()
	{
		m_drawMode = -1;
		m_size = 0;
		m_count = 0;
		m_OGLPrimitiveVertexBuffer = 0;
		m_vertexCount = 0;
		m_vertexTypeDesc = 0;
		memset(m_textureCoords, 0, sizeof(m_textureCoords));

		m_pD3DDev = 0;
		m_color = 0xff000000; // Don't know if this is correct
#ifdef _XBOX
		m_useXboxPushbufferSubmit = false;
		m_lastSetVertexShader = 0xffffffff;
#endif
	}

	~OGLPrimitiveVertexBuffer()
	{
#ifdef USE_PUSHBUFFER
		RELEASENULL(m_pushBuffer);
#endif
		delete [] m_OGLPrimitiveVertexBuffer;
	}

	HRESULT Initialize(IDirect3DDevice8* pD3DDev, IDirect3D8* pD3D, bool hardwareTandL, DWORD typeDesc)
	{
		m_pD3DDev = pD3DDev;
		if (m_vertexTypeDesc != typeDesc) 
		{
			m_vertexTypeDesc = typeDesc;
#ifdef _XBOX
			m_lastSetVertexShader = 0xffffffff;
#endif
			m_vertexSize = 0;
			if ( m_vertexTypeDesc & D3DFVF_XYZ ) 
			{
				m_vertexSize += 3 * sizeof(float);
			}
			if ( m_vertexTypeDesc & D3DFVF_DIFFUSE )
			{
				m_vertexSize += 4;
			}
			int textureStages = (m_vertexTypeDesc & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
			m_vertexSize += 2 * sizeof(float) * textureStages;
		}

#ifdef USE_PUSHBUFFER
		UINT pbSize = 384*1024; //Only used if RunUsingCpuCopy == false, it's overriden by SetPushBufferSize(), so right now it just sits here for reference
		HRESULT hr;
		hr = pD3DDev->CreatePushBuffer(pbSize, true, &m_pushBuffer);
		if ( FAILED(hr) ) 
			return hr;
#endif
		return S_OK;
	}

	DWORD GetVertexTypeDesc()
	{
		return m_vertexTypeDesc;
	}

	bool HasDevice() const
	{
		return m_pD3DDev != 0;
	}

	void EnsureDevice(IDirect3DDevice8* pD3DDev)
	{
		if (pD3DDev && m_pD3DDev != pD3DDev)
		{
#ifdef _XBOX
			static int s_xboxEnsureDeviceLogCount = 0;
			if (s_xboxEnsureDeviceLogCount < 16)
			{
				XBLF("JA: fakegl primitive vb device refresh old=%p new=%p\n",
					(void*)m_pD3DDev, (void*)pD3DDev);
			}
			s_xboxEnsureDeviceLogCount++;
#endif
			m_pD3DDev = pD3DDev;
		}
	}

	LPVOID GetOGLPrimitiveVertexBuffer()
	{
		return m_OGLPrimitiveVertexBuffer;
	}

	DWORD GetVertexCount()
	{
		return m_vertexCount;
	}

	inline void SetColor(D3DCOLOR color)
	{
		m_color = color;
	}
	
	inline void SetTextureCoord0(float u, float v)
	{
		DWORD* pCoords = (DWORD*) m_textureCoords;
		pCoords[0] = *(DWORD*)& u;
		pCoords[1] = *(DWORD*)& v;
	}

	inline void SetTextureCoord(int textStage, float u, float v)
	{
		DWORD* pCoords = (DWORD*) m_textureCoords + (textStage << 1);
		pCoords[0] = *(DWORD*)& u;
		pCoords[1] = *(DWORD*)& v;
	}

	inline void SetVertex(float x, float y, float z)
	{
		int newCount = m_count + m_vertexSize;
		if (newCount > m_size) {
			Ensure(m_vertexSize);
		}
		DWORD* pFloat = (DWORD*) (m_OGLPrimitiveVertexBuffer + m_count);
		pFloat[0] = *(DWORD*)& x;
		pFloat[1] = *(DWORD*)& y;
		pFloat[2] = *(DWORD*)& z;
		const DWORD* pCoords = (DWORD*) m_textureCoords;
		switch(m_vertexTypeDesc){
		case (D3DFVF_XYZ | D3DFVF_DIFFUSE | (1 << D3DFVF_TEXCOUNT_SHIFT)):
			pFloat[3] = m_color;
			pFloat[4] = pCoords[0];
			pFloat[5] = pCoords[1];
			break;
		case (D3DFVF_XYZ | D3DFVF_DIFFUSE | (2 << D3DFVF_TEXCOUNT_SHIFT)):
			pFloat[3] = m_color;
			pFloat[4] = pCoords[0];
			pFloat[5] = pCoords[1];
			pFloat[6] = pCoords[2];
			pFloat[7] = pCoords[3];
			break;
		default:
			{
				pFloat += 3;
				if ( m_vertexTypeDesc & D3DFVF_DIFFUSE ) 
				{
					*pFloat++ = m_color;
				}
				int textureStages = (m_vertexTypeDesc & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
				for ( int i = 0; i < textureStages; i++ )
				{
					*pFloat++ = *pCoords++;
					*pFloat++ = *pCoords++;
				}
			}
			break;
		}
		m_count = newCount;
		m_vertexCount++;

		// TO DO: Flush vertex buffer if larger than 1000 vertexes.
		// Have to do this modulo vertexes-per-primitive
	}

#ifdef _XBOX
	bool ClipTriangleListAgainstPlane(const float plane[4], const D3DMATRIX *modelView)
	{
		if (!plane || !modelView || m_vertexTypeDesc == 0 || m_vertexSize <= 0)
		{
			return false;
		}
		if (m_vertexCount < 3 || (m_vertexCount % 3) != 0)
		{
			return false;
		}

		DWORD dstVertex = 0;
		DWORD removedTris = 0;
		const DWORD srcVertexCount = m_vertexCount;

		for (DWORD srcVertex = 0; srcVertex < srcVertexCount; srcVertex += 3)
		{
			bool outside[3];
			for (int i = 0; i < 3; ++i)
			{
				const float *v = (const float *)(m_OGLPrimitiveVertexBuffer + ((srcVertex + i) * m_vertexSize));
				const float x = v[0];
				const float y = v[1];
				const float z = v[2];
				const float tx = x * modelView->_11 + y * modelView->_21 + z * modelView->_31 + modelView->_41;
				const float ty = x * modelView->_12 + y * modelView->_22 + z * modelView->_32 + modelView->_42;
				const float tz = x * modelView->_13 + y * modelView->_23 + z * modelView->_33 + modelView->_43;
				const float dist = tx * plane[0] + ty * plane[1] + tz * plane[2] + plane[3];
				outside[i] = (dist < 0.0f);
			}

			if (outside[0] && outside[1] && outside[2])
			{
				++removedTris;
				continue;
			}

			if (dstVertex != srcVertex)
			{
				memmove(m_OGLPrimitiveVertexBuffer + (dstVertex * m_vertexSize),
					m_OGLPrimitiveVertexBuffer + (srcVertex * m_vertexSize),
					3 * m_vertexSize);
			}
			dstVertex += 3;
		}

		if (removedTris)
		{
			static int s_clipTriangleLogBudget = 32;
			if (s_clipTriangleLogBudget > 0)
			{
				XBLF("JA: fakegl CPU clip trianglelist removed=%lu kept=%lu plane=%g,%g,%g,%g",
					(unsigned long)removedTris,
					(unsigned long)(dstVertex / 3),
					plane[0], plane[1], plane[2], plane[3]);
				--s_clipTriangleLogBudget;
			}
			m_vertexCount = dstVertex;
			m_count = (int)(dstVertex * m_vertexSize);
			return true;
		}

		return false;
	}

	bool IsEntireDrawOutsidePlane(const float plane[4], const D3DMATRIX *modelView) const
	{
		if (!plane || !modelView || m_vertexTypeDesc == 0 || m_vertexSize <= 0 || m_vertexCount == 0)
		{
			return false;
		}

		for (DWORD i = 0; i < m_vertexCount; ++i)
		{
			const float *v = (const float *)(m_OGLPrimitiveVertexBuffer + (i * m_vertexSize));
			const float x = v[0];
			const float y = v[1];
			const float z = v[2];
			const float tx = x * modelView->_11 + y * modelView->_21 + z * modelView->_31 + modelView->_41;
			const float ty = x * modelView->_12 + y * modelView->_22 + z * modelView->_32 + modelView->_42;
			const float tz = x * modelView->_13 + y * modelView->_23 + z * modelView->_33 + modelView->_43;
			const float dist = tx * plane[0] + ty * plane[1] + tz * plane[2] + plane[3];
			if (dist >= 0.0f)
			{
				return false;
			}
		}

		static int s_clipDrawLogBudget = 16;
		if (s_clipDrawLogBudget > 0)
		{
			XBLF("JA: fakegl CPU clip skipped draw mode=0x%08x verts=%lu plane=%g,%g,%g,%g",
				(unsigned int)m_drawMode,
				(unsigned long)m_vertexCount,
				plane[0], plane[1], plane[2], plane[3]);
			--s_clipDrawLogBudget;
		}
		return true;
	}
#endif

	inline IsMergableMode(GLenum mode)
	{
		return ( mode == m_drawMode ) && ( mode == GL_QUADS || mode == GL_TRIANGLES );
	}

	inline IsEmpty()
	{
		return m_vertexCount == 0;
	}

	inline void Begin(GLuint drawMode)
	{
		m_drawMode = drawMode;
	}

	inline void Append(GLuint drawMode)
	{
	}

	inline void End(
#ifdef _XBOX
		bool clipPlane0Enabled = false,
		const float *clipPlane0 = NULL,
		const D3DMATRIX *modelView = NULL
#endif
		)
	{
		if ( m_vertexCount == 0 ) // Startup
			return;

		D3DPRIMITIVETYPE dptPrimitiveType;
		switch ( m_drawMode ) 
		{
			case GL_POINTS: dptPrimitiveType = D3DPT_POINTLIST; break;
			case GL_LINES: dptPrimitiveType = D3DPT_LINELIST; break;
			case GL_LINE_STRIP: dptPrimitiveType = D3DPT_LINESTRIP; break;
			case GL_LINE_LOOP:
				dptPrimitiveType = D3DPT_LINESTRIP;
				LocalDebugBreak();  // Need to add one more point
				break;
			case GL_TRIANGLES: dptPrimitiveType = D3DPT_TRIANGLELIST; break;
			case GL_TRIANGLE_STRIP: dptPrimitiveType = D3DPT_TRIANGLESTRIP; break;
			case GL_TRIANGLE_FAN: dptPrimitiveType = D3DPT_TRIANGLEFAN; break;
			case GL_QUADS:
				if ( m_vertexCount <= 4 ) 
					dptPrimitiveType = D3DPT_TRIANGLEFAN;
				else 
				{
					dptPrimitiveType = D3DPT_TRIANGLELIST;
					ConvertQuadsToTriangles();
				}
				break;
			case GL_QUAD_STRIP:
				if ( m_vertexCount <= 4 ) 
					dptPrimitiveType = D3DPT_TRIANGLEFAN;
				else 
				{
					dptPrimitiveType = D3DPT_TRIANGLESTRIP;
					ConvertQuadStripToTriangleStrip();
				}
				break;

			case GL_POLYGON:
				dptPrimitiveType = D3DPT_TRIANGLEFAN;
				if ( m_vertexCount < 3) 
					goto exit;
				// How is this different from GL_TRIANGLE_FAN, other than
				// that polygons are planar?
				break;
			default:
				LocalDebugBreak();
				goto exit;
		}
#ifdef _XBOX
		if (clipPlane0Enabled)
		{
			if (dptPrimitiveType == D3DPT_TRIANGLELIST)
			{
				ClipTriangleListAgainstPlane(clipPlane0, modelView);
				if (m_vertexCount < 3)
				{
					goto exit;
				}
			}
			else if (IsEntireDrawOutsidePlane(clipPlane0, modelView))
			{
				goto exit;
			}
		}
#endif
		{
			DWORD primCount;
			switch ( dptPrimitiveType ) 
			{
				default:
				case D3DPT_TRIANGLESTRIP: primCount = m_vertexCount - 2; break;
				case D3DPT_TRIANGLEFAN: primCount = m_vertexCount - 2; break;
				case D3DPT_TRIANGLELIST: primCount = m_vertexCount / 3; break;
			}

#ifdef USE_PUSHBUFFER
			m_pD3DDev->BeginPushBuffer(m_pushBuffer);
#endif

#ifdef _XBOX
			static int s_xboxDrawLogCount = 0;
#if defined(STEFX_ELITE_FORCE_MP)
			const bool logDraw = s_xboxDrawLogCount < 32;
#else
			const bool logDraw = false;
#endif
			{
				static int s_xboxTwoStageVertexLogCount = 0;
				const int textureStages = (m_vertexTypeDesc & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
				if (textureStages >= 2 && m_vertexCount > 0 && s_xboxTwoStageVertexLogCount < 8)
				{
					const DWORD *v = (const DWORD *)m_OGLPrimitiveVertexBuffer;
					const float x = *(const float *)&v[0];
					const float y = *(const float *)&v[1];
					const float z = *(const float *)&v[2];
					const unsigned long color = (unsigned long)v[3];
					const float s0 = *(const float *)&v[4];
					const float t0 = *(const float *)&v[5];
					const float s1 = *(const float *)&v[6];
					const float t1 = *(const float *)&v[7];
					XBLF("JA: fakegl two-stage draw sample #%d mode=0x%08x prim=%d prims=%lu verts=%lu fvf=0x%08lx stride=%d xyz=%g,%g,%g color=0x%08lx st0=%g,%g st1=%g,%g",
						s_xboxTwoStageVertexLogCount,
						(unsigned int)m_drawMode,
						(int)dptPrimitiveType,
						(unsigned long)primCount,
						(unsigned long)m_vertexCount,
						(unsigned long)m_vertexTypeDesc,
						m_vertexSize,
						x, y, z,
						color,
						s0, t0,
						s1, t1);
					++s_xboxTwoStageVertexLogCount;
				}
			}
			if (logDraw)
			{
				XBLF("JA: fakegl DrawPrimitiveUP #%d dev=%p mode=0x%08x d3dPrim=%d primCount=%lu vertexCount=%lu vtxSize=%d fvf=0x%08lx vb=%p",
					s_xboxDrawLogCount, (void*)m_pD3DDev, (unsigned int)m_drawMode, (int)dptPrimitiveType,
					(unsigned long)primCount, (unsigned long)m_vertexCount, m_vertexSize,
					(unsigned long)m_vertexTypeDesc, m_OGLPrimitiveVertexBuffer);
			}
#endif
			if (m_pD3DDev)
			{
				HRESULT hrSetVertexShader = S_OK;
#ifdef _XBOX
				if (logDraw)
				{
					XBLF("JA: fakegl SetVertexShader #%d fvf=0x%08lx",
						s_xboxDrawLogCount, (unsigned long)m_vertexTypeDesc);
				}
				if (m_lastSetVertexShader != m_vertexTypeDesc)
				{
					hrSetVertexShader = m_pD3DDev->SetVertexShader(m_vertexTypeDesc);
					m_lastSetVertexShader = m_vertexTypeDesc;
				}
#else
				hrSetVertexShader = m_pD3DDev->SetVertexShader(m_vertexTypeDesc);
#endif
#ifdef _XBOX
				if (logDraw)
				{
					XBLF("JA: fakegl SetVertexShader #%d hr=0x%08lx",
						s_xboxDrawLogCount, (unsigned long)hrSetVertexShader);
				}
#endif
				HRESULT hrDrawPrimitive = S_OK;
#ifdef _XBOX
				if (!TryPushbufferPrimitiveXbox(dptPrimitiveType, primCount, m_OGLPrimitiveVertexBuffer))
				{
					hrDrawPrimitive = DrawPrimitiveUPXbox(dptPrimitiveType, primCount, m_OGLPrimitiveVertexBuffer);
				}
#else
				hrDrawPrimitive = DrawPrimitiveUPXbox(dptPrimitiveType, primCount, m_OGLPrimitiveVertexBuffer);
#endif
#ifdef _XBOX
				if (logDraw)
				{
					XBLF("JA: fakegl DrawPrimitiveUP #%d returned hr=0x%08lx", s_xboxDrawLogCount, (unsigned long)hrDrawPrimitive);
				}
#endif
			}
#ifdef _XBOX
			else
			{
				XBLog_Write("JA: fakegl DrawPrimitiveUP skipped because m_pD3DDev is NULL");
			}
			s_xboxDrawLogCount++;
#endif

#ifdef USE_PUSHBUFFER
			m_pD3DDev->EndPushBuffer();
			m_pD3DDev->RunPushBuffer(m_pushBuffer, NULL);
#endif
		}
exit:
		m_vertexCount = 0;
		m_count = 0;
	}

private:
#ifdef _XBOX
	DWORD VertexCountForPrimitive(D3DPRIMITIVETYPE dptPrimitiveType, DWORD primCount) const
	{
		switch (dptPrimitiveType)
		{
		case D3DPT_POINTLIST:
			return primCount;
		case D3DPT_LINELIST:
			return primCount * 2;
		case D3DPT_LINESTRIP:
		case D3DPT_TRIANGLESTRIP:
		case D3DPT_TRIANGLEFAN:
			return primCount + 2;
		case D3DPT_TRIANGLELIST:
			return primCount * 3;
		default:
			return 0;
		}
	}

	DWORD PrimitiveChunkLimit(D3DPRIMITIVETYPE dptPrimitiveType, DWORD maxVerts) const
	{
		if (maxVerts == 0)
		{
			return 0;
		}

		switch (dptPrimitiveType)
		{
		case D3DPT_POINTLIST:
			return maxVerts;
		case D3DPT_LINELIST:
			return maxVerts / 2;
		case D3DPT_TRIANGLELIST:
			return maxVerts / 3;
		default:
			return 0;
		}
	}

	bool PushbufferSubmitChunkXbox(D3DPRIMITIVETYPE dptPrimitiveType, DWORD vertexCount, const void *vertices)
	{
		if (!m_pD3DDev || !vertices || m_vertexSize <= 0 || vertexCount == 0)
		{
			return false;
		}

		const DWORD strideDwords = (DWORD)(m_vertexSize / sizeof(DWORD));
		const DWORD vertexWords = strideDwords * vertexCount;
		DWORD *push = NULL;

		m_pD3DDev->BeginPush(vertexWords + 5, &push);
		if (!push)
		{
			return false;
		}

		push[0] = D3DPUSH_ENCODE(D3DPUSH_SET_BEGIN_END, 1);
		push[1] = dptPrimitiveType;
		push[2] = D3DPUSH_ENCODE(D3DPUSH_NOINCREMENT_FLAG | D3DPUSH_INLINE_ARRAY, vertexWords);
		push += 3;

		memcpy(push, vertices, vertexWords * sizeof(DWORD));
		push += vertexWords;

		push[0] = D3DPUSH_ENCODE(D3DPUSH_SET_BEGIN_END, 1);
		push[1] = 0;
		push += 2;

		m_pD3DDev->EndPush(push);
		return true;
	}

	bool TryPushbufferPrimitiveXbox(D3DPRIMITIVETYPE dptPrimitiveType, DWORD primCount, const void *vertices)
	{
		static int s_xboxPushSubmitLogCount = 0;
		static int s_xboxPushFallbackLogCount = 0;

		if (!m_useXboxPushbufferSubmit || !m_pD3DDev || !vertices || m_vertexSize <= 0)
		{
			return false;
		}

		if ((m_vertexSize % sizeof(DWORD)) != 0)
		{
			if (s_xboxPushFallbackLogCount < 8)
			{
				XBLF("JA: fakegl push submit fallback unaligned stride=%d", m_vertexSize);
				++s_xboxPushFallbackLogCount;
			}
			return false;
		}

		const DWORD strideDwords = (DWORD)(m_vertexSize / sizeof(DWORD));
		const DWORD maxPacketWords = 2040;
		const DWORD maxVerts = maxPacketWords / strideDwords;
		const DWORD vertexCount = VertexCountForPrimitive(dptPrimitiveType, primCount);

		if (maxVerts == 0 || vertexCount == 0)
		{
			return false;
		}

		if (vertexCount <= maxVerts)
		{
			const bool pushed = PushbufferSubmitChunkXbox(dptPrimitiveType, vertexCount, vertices);
			if (pushed)
			{
				g_SPXBFakeGLPrimitiveCalls++;
				g_SPXBFakeGLPrimitiveVerts += vertexCount;
				if (s_xboxPushSubmitLogCount < 8)
				{
					XBLF("JA: fakegl push submit #%d type=%d prims=%lu verts=%lu stride=%d fvf=0x%08lx",
						s_xboxPushSubmitLogCount,
						(int)dptPrimitiveType,
						(unsigned long)primCount,
						(unsigned long)vertexCount,
						m_vertexSize,
						(unsigned long)m_vertexTypeDesc);
					++s_xboxPushSubmitLogCount;
				}
			}
			return pushed;
		}

		const DWORD chunkPrimsLimit = PrimitiveChunkLimit(dptPrimitiveType, maxVerts);
		if (chunkPrimsLimit == 0)
		{
			if (s_xboxPushFallbackLogCount < 8)
			{
				XBLF("JA: fakegl push submit fallback unsplittable type=%d prims=%lu verts=%lu maxVerts=%lu",
					(int)dptPrimitiveType,
					(unsigned long)primCount,
					(unsigned long)vertexCount,
					(unsigned long)maxVerts);
				++s_xboxPushFallbackLogCount;
			}
			return false;
		}

		const char *base = (const char *)vertices;
		DWORD primBase;
		for (primBase = 0; primBase < primCount; )
		{
			DWORD chunkPrims = primCount - primBase;
			DWORD chunkVerts;
			const char *chunkVertices;

			if (chunkPrims > chunkPrimsLimit)
			{
				chunkPrims = chunkPrimsLimit;
			}
			chunkVerts = VertexCountForPrimitive(dptPrimitiveType, chunkPrims);
			chunkVertices = base + (VertexCountForPrimitive(dptPrimitiveType, primBase) * m_vertexSize);

			if (!PushbufferSubmitChunkXbox(dptPrimitiveType, chunkVerts, chunkVertices))
			{
				return false;
			}
			g_SPXBFakeGLPrimitiveCalls++;
			g_SPXBFakeGLPrimitiveVerts += chunkVerts;
			primBase += chunkPrims;
		}

		if (s_xboxPushSubmitLogCount < 8)
		{
			XBLF("JA: fakegl push submit #%d chunked type=%d prims=%lu verts=%lu stride=%d fvf=0x%08lx",
				s_xboxPushSubmitLogCount,
				(int)dptPrimitiveType,
				(unsigned long)primCount,
				(unsigned long)vertexCount,
				m_vertexSize,
				(unsigned long)m_vertexTypeDesc);
			++s_xboxPushSubmitLogCount;
		}
		return true;
	}
#endif

	HRESULT DrawPrimitiveUPXbox(D3DPRIMITIVETYPE dptPrimitiveType, DWORD primCount, const void *vertices)
	{
#ifdef _XBOX
		static int s_xboxChunkLogCount = 0;
		static int s_xboxSubmitLogCount = 0;
		static DWORD s_xboxDrawSubmitCount = 0;
		const DWORD maxTriangleListPrims = 2048;
		const bool stefxLateSubmit = false;
		HRESULT firstFailure = S_OK;

		if (!m_pD3DDev)
		{
			return D3DERR_INVALIDCALL;
		}

		if (dptPrimitiveType == D3DPT_TRIANGLELIST && primCount > maxTriangleListPrims)
		{
				DWORD primBase;
				const char *base = (const char *)vertices;

				for (primBase = 0; primBase < primCount; )
				{
					DWORD chunkPrims = primCount - primBase;
					HRESULT hr;
					const bool xboxTraceSubmit = stefxLateSubmit;

				if (chunkPrims > maxTriangleListPrims)
				{
					chunkPrims = maxTriangleListPrims;
				}

				if (false && s_xboxChunkLogCount < 32)
				{
					XBLF("JA: fakegl DrawPrimitiveUP chunk type=%d primBase=%lu chunk=%lu total=%lu submit=%lu",
						(int)dptPrimitiveType,
						(unsigned long)primBase,
						(unsigned long)chunkPrims,
						(unsigned long)primCount,
						(unsigned long)s_xboxDrawSubmitCount);
					s_xboxChunkLogCount++;
				}

				if (xboxTraceSubmit || s_xboxSubmitLogCount < 16)
				{
					const void *chunkVerts = base + (primBase * 3 * m_vertexSize);
					XBLF("%s fakegl DrawPrimitiveUP submit #%lu chunk pre frame=%d type=%d prims=%lu verts=%p stride=%d bytes=%lu",
						stefxLateSubmit ? "STEFX: LATE" : "JA:",
						(unsigned long)s_xboxDrawSubmitCount, g_stefxFakeglSwapFrame,
						(int)dptPrimitiveType, (unsigned long)chunkPrims, chunkVerts,
						m_vertexSize, (unsigned long)(chunkPrims * 3 * m_vertexSize));
				}
				hr = m_pD3DDev->DrawPrimitiveUP(dptPrimitiveType,
					chunkPrims,
					base + (primBase * 3 * m_vertexSize),
					m_vertexSize);
				g_SPXBFakeGLPrimitiveCalls++;
				g_SPXBFakeGLPrimitiveVerts += chunkPrims * 3;
				if (xboxTraceSubmit || s_xboxSubmitLogCount < 16)
				{
					XBLF("%s fakegl DrawPrimitiveUP submit #%lu chunk post frame=%d hr=0x%08lx",
						stefxLateSubmit ? "STEFX: LATE" : "JA:",
						(unsigned long)s_xboxDrawSubmitCount, g_stefxFakeglSwapFrame,
						(unsigned long)hr);
				}
				if (FAILED(hr) && SUCCEEDED(firstFailure))
				{
					firstFailure = hr;
				}

				if (xboxTraceSubmit || s_xboxSubmitLogCount < 16)
				{
					XBLF("%s fakegl DrawPrimitiveUP submit #%lu complete frame=%d",
						stefxLateSubmit ? "STEFX: LATE" : "JA:",
						(unsigned long)s_xboxDrawSubmitCount, g_stefxFakeglSwapFrame);
					if (s_xboxSubmitLogCount < 16)
					{
						++s_xboxSubmitLogCount;
					}
				}
				s_xboxDrawSubmitCount++;
				primBase += chunkPrims;
			}

			return firstFailure;
		}

		{
			const bool xboxTraceSubmit = stefxLateSubmit;
			if (xboxTraceSubmit || s_xboxSubmitLogCount < 16)
			{
				DWORD vertexCount = VertexCountForPrimitive(dptPrimitiveType, primCount);
				XBLF("%s fakegl DrawPrimitiveUP submit #%lu direct pre frame=%d type=%d prims=%lu verts=%lu ptr=%p stride=%d bytes=%lu",
					stefxLateSubmit ? "STEFX: LATE" : "JA:",
					(unsigned long)s_xboxDrawSubmitCount, g_stefxFakeglSwapFrame,
					(int)dptPrimitiveType, (unsigned long)primCount,
					(unsigned long)vertexCount, vertices, m_vertexSize,
					(unsigned long)(vertexCount * m_vertexSize));
			}
			HRESULT hr = m_pD3DDev->DrawPrimitiveUP(dptPrimitiveType, primCount, vertices, m_vertexSize);
			g_SPXBFakeGLPrimitiveCalls++;
			switch (dptPrimitiveType)
			{
			case D3DPT_TRIANGLELIST:
				g_SPXBFakeGLPrimitiveVerts += primCount * 3;
				break;
			case D3DPT_TRIANGLESTRIP:
			case D3DPT_TRIANGLEFAN:
				g_SPXBFakeGLPrimitiveVerts += primCount + 2;
				break;
			case D3DPT_LINELIST:
				g_SPXBFakeGLPrimitiveVerts += primCount * 2;
				break;
			case D3DPT_LINESTRIP:
				g_SPXBFakeGLPrimitiveVerts += primCount + 1;
				break;
			case D3DPT_POINTLIST:
				g_SPXBFakeGLPrimitiveVerts += primCount;
				break;
			default:
				g_SPXBFakeGLPrimitiveVerts += primCount;
				break;
			}
			if (xboxTraceSubmit || s_xboxSubmitLogCount < 16)
			{
				XBLF("%s fakegl DrawPrimitiveUP submit #%lu direct post frame=%d hr=0x%08lx",
					stefxLateSubmit ? "STEFX: LATE" : "JA:",
					(unsigned long)s_xboxDrawSubmitCount, g_stefxFakeglSwapFrame,
					(unsigned long)hr);
			}
			if (xboxTraceSubmit || s_xboxSubmitLogCount < 16)
			{
				XBLF("%s fakegl DrawPrimitiveUP submit #%lu complete frame=%d",
					stefxLateSubmit ? "STEFX: LATE" : "JA:",
					(unsigned long)s_xboxDrawSubmitCount, g_stefxFakeglSwapFrame);
				if (s_xboxSubmitLogCount < 16)
				{
					++s_xboxSubmitLogCount;
				}
			}
			s_xboxDrawSubmitCount++;
			return hr;
		}
#else
		return m_pD3DDev->DrawPrimitiveUP(dptPrimitiveType, primCount, vertices, m_vertexSize);
#endif
	}

	void ConvertQuadsToTriangles()
	{
		int quadCount = m_vertexCount / 4;
		int addedVerticies = 2 * quadCount;
		int addedDataSize = addedVerticies * m_vertexSize;
		Ensure( addedDataSize );

		// A quad is v0, v1, v2, v3
		// The corresponding triangle pair is v0 v1 v2 , v0 v2 v3
		for(int i = quadCount-1; i >= 0; i--)
		{
			int startOfQuad = i * m_vertexSize * 4;
			int startOfTrianglePair = i * m_vertexSize * 6;
			// Copy the last two verticies of the second triangle
			memcpy(m_OGLPrimitiveVertexBuffer + startOfTrianglePair + 4 * m_vertexSize,
				m_OGLPrimitiveVertexBuffer + startOfQuad + m_vertexSize * 2, m_vertexSize * 2);
			// Copy the first vertex of the second triangle
			memcpy(m_OGLPrimitiveVertexBuffer + startOfTrianglePair + 3 * m_vertexSize,
				m_OGLPrimitiveVertexBuffer + startOfQuad, m_vertexSize);
			// Copy the first triangle
			if ( i > 0 ) 
			{
				memcpy(m_OGLPrimitiveVertexBuffer + startOfTrianglePair, m_OGLPrimitiveVertexBuffer + startOfQuad, 3 * m_vertexSize);
			}
		}
		m_count += addedDataSize;
		m_vertexCount += addedVerticies;
	}

	void ConvertQuadStripToTriangleStrip()
	{
		int vertexPairCount = m_vertexCount / 2;

		// Doesn't add any points, but does reorder the vertices.
		// Swap each pair of verticies.

		for(int i = 0; i < vertexPairCount; i++) 
		{
			int startOfPair = i * m_vertexSize * 2;
			int middleOfPair = startOfPair + m_vertexSize;
			for(int j = 0; j < m_vertexSize; j++) 
			{
				int c = m_OGLPrimitiveVertexBuffer[startOfPair + j];
				m_OGLPrimitiveVertexBuffer[startOfPair + j] = m_OGLPrimitiveVertexBuffer[middleOfPair + j];
				m_OGLPrimitiveVertexBuffer[middleOfPair + j] = c;
			}
		}
	}

	void Ensure(int size)
	{
		if (( m_count + size ) > m_size ) 
		{
			int newSize = m_size * 2;
			if ( newSize < m_count + size ) newSize = m_count + size;
			char* newVB = new char[newSize];
			if ( m_OGLPrimitiveVertexBuffer )
			{
				memcpy(newVB, m_OGLPrimitiveVertexBuffer, m_count);
			}
			delete[] m_OGLPrimitiveVertexBuffer;
			m_OGLPrimitiveVertexBuffer = newVB;
			m_size = newSize;
		}

		/*
		char buf[512];
		sprintf(buf, "Current size %d\n", m_size);
		OutputDebugString(buf);
		*/
	}

	GLuint m_drawMode;
	DWORD  m_vertexTypeDesc;
	int m_vertexSize; // in bytes

	IDirect3DDevice8* m_pD3DDev;
	char* m_OGLPrimitiveVertexBuffer;
	int m_size;  // bytes size of buffer
	int m_count; // bytes used
	DWORD m_vertexCount;
	D3DCOLOR m_color;
	float m_textureCoords[MAXSTATES*2];
	IDirect3DPushBuffer8* m_pushBuffer;
#ifdef _XBOX
	bool m_useXboxPushbufferSubmit;
	DWORD m_lastSetVertexShader;
#endif
};

#endif // USE_DRAWPRIMITIVE

class FakeGL
{
private:
	IDirect3DDevice8* m_pD3DDev;
    D3DSURFACE_DESC m_d3dsdBackBuffer;   // Surface desc of the backbuffer
	LPDIRECT3D8 m_pD3D;
	
	IDirect3DTexture8* m_pPrimary;
	IDirect3DTexture8* m_fallbackTexture;
	bool m_hardwareTandL;
	
    bool m_bD3DXReady;
	
	bool m_glRenderStateDirty;

	bool m_glAlphaStateDirty;
	GLenum m_glAlphaFunc;
	GLclampf m_glAlphaFuncRef;
	bool m_glAlphaTest;

	bool m_glBlendStateDirty;
	bool m_glBlend;
	GLenum m_glBlendFuncSFactor;
	GLenum m_glBlendFuncDFactor;

	bool m_glCullStateDirty;
	bool m_glCullFace;
	GLenum m_glCullFaceMode;
	
	bool m_glDepthStateDirty;
	bool m_glDepthTest;
	GLenum m_glDepthFunc;
	bool m_glDepthMask;
	bool m_glClipPlane0StateDirty;
	bool m_glClipPlane0Enabled;
	float m_glClipPlane0[4];
	bool m_glScissorTest;
	D3DRECT m_glScissorRect;
	bool m_glFogStateDirty;
	bool m_glFog;
	GLint m_glFogMode;
	GLfloat m_glFogDensity;
	GLfloat m_glFogStart;
	GLfloat m_glFogEnd;
	D3DCOLOR m_glFogColor;

	GLclampd m_glDepthRangeNear;
	GLclampd m_glDepthRangeFar;

	GLenum m_glMatrixMode;

	GLenum m_glPolygonModeFront;
	GLenum m_glPolygonModeBack;

	bool m_glShadeModelStateDirty;
	GLenum m_glShadeModel;

	bool m_bViewPortDirty;
	GLint m_glViewPortX;
	GLint m_glViewPortY;
	GLsizei m_glViewPortWidth;
	GLsizei m_glViewPortHeight;
	D3DCOLOR m_glClearColor;

	TextureState m_textureState;
	TextureTable m_textures;

	bool m_modelViewMatrixStateDirty;
	bool m_projectionMatrixStateDirty;
	bool m_textureMatrixStateDirty;
	bool* m_currentMatrixStateDirty; // an alias to one of the preceeding stacks

	ID3DXMatrixStack* m_modelViewMatrixStack;
	ID3DXMatrixStack* m_projectionMatrixStack;
	ID3DXMatrixStack* m_textureMatrixStack;
	ID3DXMatrixStack* m_currentMatrixStack; // an alias to one of the preceeding stacks

	bool m_viewMatrixStateDirty;
	D3DXMATRIX m_d3dViewMatrix;

	OGLPrimitiveVertexBuffer m_OGLPrimitiveVertexBuffer;

	bool m_needBeginScene;

	const char* m_vendor;
	const char* m_renderer;
	char m_version[64];
	const char* m_extensions;
	D3DADAPTER_IDENTIFIER8 m_dddi;
	DWORD m_windowHeight;

	char* m_stickyAlloc;
	DWORD m_stickyAllocSize;
	char* m_subImageScratch;
	DWORD m_subImageScratchSize;
#ifdef _XBOX
	bool m_stefxEliteForceScriptPanelDraw;
#endif

	bool m_hintGenerateMipMaps;

	HRESULT ReleaseD3DX()
	{
		m_bD3DXReady = FALSE;
		return S_OK;
	}
	
	HRESULT InitD3DX()
	{
		HRESULT hr;

#ifdef _XBOX
		XBL("JA: fakegl InitD3DX enter\n");
#endif
		m_pD3D = Direct3DCreate8( D3D_SDK_VERSION );
#ifdef _XBOX
		XBLF("JA: fakegl Direct3DCreate8 -> %p\n", (void*)m_pD3D);
#endif

		// Set up the structure used to create the D3DDevice.
		D3DPRESENT_PARAMETERS params; 
		ZeroMemory( &params, sizeof(params) );

		// Set up the parameters
		params.EnableAutoDepthStencil = TRUE;
		/* Raven/VV's Xbox DX8 present path and the retail SP XBE both use
		 * the linear D24S8 depth/stencil format. Matching it matters for
		 * stable depth ordering on Xbox/Cxbx rather than merely allocating
		 * a depth surface with the same bit count. */
		params.AutoDepthStencilFormat = D3DFMT_LIN_D24S8;
		/* CXBX-R cross-project audit: SwapEffect_DISCARD (not FLIP) +
		 * PresentationInterval_IMMEDIATE (not default ONE) + BackBufferCount=1
		 * + Windowed=FALSE + hDeviceWindow=NULL. This is the identical
		 * D3DPRESENT_PARAMETERS block UT99-Xbox / TheForceEngine / OpenJKDF2
		 * all use. fakegl's FLIP+ONE default works on real silicon eventually,
		 * but CXBX-R's HLE vsync emulation deadlocks. */
		params.SwapEffect             = D3DSWAPEFFECT_DISCARD;
		params.BackBufferCount        = 1;
		params.BackBufferWidth        = gWidth;
		params.BackBufferHeight       = gHeight;
		params.BackBufferFormat       = D3DFMT_A8R8G8B8;
		params.Windowed               = FALSE;
		params.hDeviceWindow          = NULL;
		params.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;
		params.FullScreen_RefreshRateInHz = D3DPRESENT_RATE_DEFAULT;

		/* CXBX-R audit: do not call SetPushBufferSize here. Prior local
		 * testing found it can collide with auto-depth-stencil allocation;
		 * the default Xbox D3D8 push-buffer setup is the stable path. */
		// Create the Direct3D device.
		hr = Direct3D_CreateDevice( 0, D3DDEVTYPE_HAL, NULL,
		                            D3DCREATE_HARDWARE_VERTEXPROCESSING,
		                            &params, &m_pD3DDev );
#ifdef _XBOX
		XBLF("JA: fakegl CreateDevice -> hr=0x%08x dev=%p via=%s\n",
			(unsigned int)hr, (void*)m_pD3DDev, "static");
#endif
		if( FAILED(hr) )
			return hr;

		// Store render target surface desc.  Cxbx-R's HLE path can create the
		// device successfully and still fault when querying the implicit
		// backbuffer surface here; the values are the present params we own.
		ZeroMemory(&m_d3dsdBackBuffer, sizeof(m_d3dsdBackBuffer));
		m_d3dsdBackBuffer.Format = params.BackBufferFormat;
		m_d3dsdBackBuffer.Width = params.BackBufferWidth;
		m_d3dsdBackBuffer.Height = params.BackBufferHeight;
		m_d3dsdBackBuffer.Type = D3DRTYPE_SURFACE;
#ifdef _XBOX
		__try
		{
			LPDIRECT3DSURFACE8 pBackBuffer = NULL;
			HRESULT hrBackBuffer = m_pD3DDev->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer);
			XBLF("JA: fakegl init GetBackBuffer hr=0x%08x surf=%p\n",
				(unsigned int)hrBackBuffer, (void*)pBackBuffer);
			if (SUCCEEDED(hrBackBuffer) && pBackBuffer)
			{
				D3DSURFACE_DESC desc;
				HRESULT hrDesc = pBackBuffer->GetDesc(&desc);
				XBLF("JA: fakegl init backbuffer desc hr=0x%08x fmt=0x%08x size=%ux%u type=%u\n",
					(unsigned int)hrDesc,
					(unsigned int)desc.Format,
					(unsigned int)desc.Width,
					(unsigned int)desc.Height,
					(unsigned int)desc.Type);
				if (SUCCEEDED(hrDesc))
				{
					m_d3dsdBackBuffer = desc;
				}
				pBackBuffer->Release();
			}
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			XBLog_Write("JA: fakegl init GetBackBuffer probe excepted; using present params");
		}
#endif
#ifdef _XBOX
		XBLF("JA: fakegl backbuffer fmt=0x%08x size=%ux%u\n",
			(unsigned int)m_d3dsdBackBuffer.Format,
			(unsigned int)m_d3dsdBackBuffer.Width,
			(unsigned int)m_d3dsdBackBuffer.Height);
#endif

		// Apply visual improvements
		m_pD3DDev->SetFlickerFilter(1);
		m_pD3DDev->SetSoftDisplayFilter(false);

		// We are done here
		m_bD3DXReady = TRUE;
#ifdef _XBOX
		EnsureFallbackTexture();
#endif
#ifdef _XBOX
		XBL("JA: fakegl InitD3DX done\n");
#endif

		return hr;
	}
	
	void InterpretError(HRESULT hr)
	{
		char errStr[100];
		D3DXGetErrorString(hr, errStr, sizeof(errStr) / sizeof(errStr[0]) );
#ifdef _XBOX
		XBLF("JA: fakegl HRESULT 0x%08x %s\n", (unsigned int)hr, errStr);
#endif
		OutputDebugString(errStr);
		LocalDebugBreak();
	}

	int BytesPerPixel(D3DFORMAT format)
	{
		switch ( format ) 
		{
			case D3DFMT_P8:
			case D3DFMT_AL8:
			case D3DFMT_A8:
			case D3DFMT_L8:
				return 1;

			case D3DFMT_R5G6B5:
			case D3DFMT_A4R4G4B4:
			case D3DFMT_A8L8:
				return 2;

			case D3DFMT_A8R8G8B8:
			case D3DFMT_X8R8G8B8:
				return 4;

			case D3DFMT_DXT1:
			case D3DFMT_DXT3:
			case D3DFMT_DXT5:
				return 0;

			default:
				LocalDebugBreak();
				return 4;
		}
	}

	DWORD DDSLevelRowBytes(D3DFORMAT format, DWORD width)
	{
		switch ( format )
		{
			case D3DFMT_DXT1:
				return ((width + 3) >> 2) * 8;
			case D3DFMT_DXT3:
			case D3DFMT_DXT5:
				return ((width + 3) >> 2) * 16;
			default:
				return width * BytesPerPixel(format);
		}
	}

	DWORD DDSLevelRows(D3DFORMAT format, DWORD height)
	{
		switch ( format )
		{
			case D3DFMT_DXT1:
			case D3DFMT_DXT3:
			case D3DFMT_DXT5:
				return (height + 3) >> 2;
			default:
				return height;
		}
	}

#ifdef _XBOX
	DWORD EstimateTextureBytes(D3DFORMAT format, DWORD width, DWORD height, int levels)
	{
		DWORD total = 0;
		DWORD levelWidth = width;
		DWORD levelHeight = height;
		if (levels <= 0)
			levels = 1;
		for (int level = 0; level < levels; ++level)
		{
			total += DDSLevelRowBytes(format, levelWidth) * DDSLevelRows(format, levelHeight);
			if (levelWidth > 1)
				levelWidth >>= 1;
			if (levelHeight > 1)
				levelHeight >>= 1;
		}
		return total;
	}

	int DDSAvailableLevels(D3DFORMAT format, DWORD width, DWORD height, DWORD bytes)
	{
		int levels = 0;
		while (width > 0 && height > 0)
		{
			DWORD levelBytes = DDSLevelRowBytes(format, width) * DDSLevelRows(format, height);
			if (!levelBytes || bytes < levelBytes)
				break;
			bytes -= levelBytes;
			++levels;
			if (width == 1 && height == 1)
				break;
			if (width > 1)
				width >>= 1;
			if (height > 1)
				height >>= 1;
		}
		return levels;
	}

	void TrackTextureAlloc(const char *kind, DWORD bytes)
	{
		g_fakeglTextureCount++;
		g_fakeglTextureBytes += bytes;
		if (g_fakeglTextureCount <= 16 || (g_fakeglTextureCount & 63) == 0)
		{
			XBLF("JA: fakegl texture budget kind=%s count=%u bytes=%u totalKB=%u failures=%u\n",
				kind, g_fakeglTextureCount, bytes, (DWORD)(g_fakeglTextureBytes / 1024),
				g_fakeglTextureFailures);
		}
	}

	void TrackTextureFailure(const char *where, DWORD width, DWORD height, DWORD levels,
		GLint internalformat, GLenum format, D3DFORMAT destPixelFormat, HRESULT hr,
		DWORD requestBytes)
	{
		g_fakeglTextureFailures++;
		XBLog_Writef("STEFX_TEX_FAIL where=%s image='%s' tex=%d size=%ux%u levels=%u internal=0x%08x format=0x%08x dest=0x%08x hr=0x%08lx request=%u failures=%u",
			where ? where : "(null)",
			FakeGL_CurrentTextureDebugName(),
			m_textures.GetCurrentID(),
			(unsigned int)width,
			(unsigned int)height,
			(unsigned int)levels,
			(unsigned int)internalformat,
			(unsigned int)format,
			(unsigned int)destPixelFormat,
			(unsigned long)hr,
			(unsigned int)requestBytes,
			(unsigned int)g_fakeglTextureFailures);
	}

	void LogTextureMemoryPressure(const char *where, DWORD requestBytes, HRESULT hr)
	{
		static int s_memoryPressureLogs = 0;
		if (s_memoryPressureLogs >= 96)
		{
			return;
		}

		MEMORYSTATUS stat;
		GlobalMemoryStatus(&stat);
		XBLF("JA: fakegl texture memory where=%s request=%u hr=0x%08lx physFreeKB=%lu virtFreeKB=%lu texCount=%u texKB=%u failures=%u regCount=%u regKB=%u regDenied=%u poolUsedKB=%lu poolFreeKB=%lu poolCapKB=%lu\n",
			where ? where : "(null)",
			(unsigned int)requestBytes,
			(unsigned long)hr,
			(unsigned long)(stat.dwAvailPhys / 1024),
			(unsigned long)(stat.dwAvailVirtual / 1024),
			(unsigned int)g_fakeglTextureCount,
			(unsigned int)(g_fakeglTextureBytes / 1024),
			(unsigned int)g_fakeglTextureFailures,
			(unsigned int)g_fakeglRegisteredTextureCount,
			(unsigned int)(g_fakeglRegisteredTextureBytes / 1024),
			(unsigned int)g_fakeglRegisteredTextureDenied,
			(unsigned long)(JkaStaticTextureUsed() / 1024),
			(unsigned long)(JkaStaticTextureFree() / 1024),
			(unsigned long)(JkaStaticTextureCapacity() / 1024));
		++s_memoryPressureLogs;
	}

	bool RegisteredTextureBudgetAllows(DWORD bytes, const char *kind)
	{
		unsigned long poolFree = JkaStaticTextureFree();
		if (!bytes ||
			g_fakeglRegisteredTextureBytes + bytes > FAKEGL_REGISTERED_TEXTURE_SOFT_CAP ||
			poolFree < bytes + FAKEGL_REGISTERED_TEXTURE_MIN_FREE)
		{
			++g_fakeglRegisteredTextureDenied;
			XBLF("JA: fakegl registered texture denied kind=%s tex=%d bytes=%u regKB=%u capKB=%u poolFreeKB=%lu reserveKB=%u denied=%u\n",
				kind ? kind : "(null)",
				m_textures.GetCurrentID(),
				(unsigned int)bytes,
				(unsigned int)(g_fakeglRegisteredTextureBytes / 1024),
				(unsigned int)(FAKEGL_REGISTERED_TEXTURE_SOFT_CAP / 1024),
				(unsigned long)(poolFree / 1024),
				(unsigned int)(FAKEGL_REGISTERED_TEXTURE_MIN_FREE / 1024),
				(unsigned int)g_fakeglRegisteredTextureDenied);
			LogTextureMemoryPressure("registered-denied", bytes, E_OUTOFMEMORY);
			return false;
		}
		return true;
	}
#endif

	HRESULT CreateXboxTexture(DWORD width, DWORD height, DWORD levels, DWORD usage,
		D3DFORMAT format, IDirect3DTexture8** texture)
	{
		if (!texture)
			return E_FAIL;
		*texture = NULL;
#ifdef _XBOX
		D3DTexture* created = D3DDevice_CreateTexture2(width, height, 1, levels, usage, format, D3DRTYPE_TEXTURE);
		if (!created)
			return E_OUTOFMEMORY;
		*texture = created;
		return S_OK;
#else
		return m_pD3DDev->CreateTexture(width, height, levels, usage, format, D3DPOOL_MANAGED, texture);
#endif
	}

#ifdef _XBOX
	HRESULT CreateRegisteredXboxTexture(DWORD width, DWORD height, DWORD levels, DWORD usage,
		D3DFORMAT format, IDirect3DTexture8** texture, DWORD* textureBytes, void** textureData = NULL)
	{
		if (!texture)
			return E_FAIL;
		*texture = NULL;
		if (textureBytes)
			*textureBytes = 0;
		if (textureData)
			*textureData = NULL;

		D3DTexture* header = new D3DTexture;
		if (!header)
			return E_OUTOFMEMORY;
		ZeroMemory(header, sizeof(*header));

		DWORD bytes = XGSetTextureHeader(width, height, levels, usage, format,
			D3DPOOL_DEFAULT, header, 0, 0);
		if (!bytes)
		{
			delete header;
			return E_FAIL;
		}
		if (!RegisteredTextureBudgetAllows(bytes, "rgba"))
		{
			delete header;
			return E_OUTOFMEMORY;
		}

		void* textureMemory = NULL;
		try
		{
			textureMemory = JkaStaticTextureAlloc(bytes, (GLuint)m_textures.GetCurrentID());
		}
		catch (...)
		{
			textureMemory = NULL;
		}
		if (!textureMemory)
		{
			XBLF("JA: fakegl registered DDS pool allocation failed tex=%d bytes=%u\n",
				m_textures.GetCurrentID(), bytes);
			LogTextureMemoryPressure("registered-pool-null", bytes, E_OUTOFMEMORY);
			delete header;
			return E_OUTOFMEMORY;
		}

		header->Register(textureMemory);
		++g_fakeglRegisteredTextureCount;
		g_fakeglRegisteredTextureBytes += bytes;
		*texture = header;
		if (textureBytes)
			*textureBytes = bytes;
		if (textureData)
			*textureData = textureMemory;
		{
			static int s_efRegisteredTextureLogBudget = 40;
			if (s_efRegisteredTextureLogBudget > 0)
			{
				XBLF("EF: TEX_REGISTERED tex=%d size=%ux%u levels=%u format=0x%08x bytes=%u data=%p",
					m_textures.GetCurrentID(),
					(unsigned int)width,
					(unsigned int)height,
					(unsigned int)levels,
					(unsigned int)format,
					(unsigned int)bytes,
					textureMemory);
				--s_efRegisteredTextureLogBudget;
			}
		}
		return S_OK;
	}

	bool UploadRegisteredLinearTexture(D3DFORMAT destPixelFormat, GLsizei width, GLsizei height,
		const char* compatablePixels, DWORD compatablePixelsPitch,
		void* registeredTextureData, DWORD registeredTextureBytes)
	{
		if (!compatablePixels || !registeredTextureData || width <= 0 || height <= 0)
		{
			return false;
		}

		const int bytesPerPixel = BytesPerPixel(destPixelFormat);
		if (bytesPerPixel <= 0)
		{
			XBLF("JA: fakegl registered linear upload rejected tex=%d size=%dx%d format=0x%08x bpp=%d\n",
				m_textures.GetCurrentID(), width, height,
				(unsigned int)destPixelFormat, bytesPerPixel);
			return false;
		}

		const DWORD rowBytes = (DWORD)width * (DWORD)bytesPerPixel;
		const DWORD requiredBytes = rowBytes * (DWORD)height;
		if (compatablePixelsPitch < rowBytes || registeredTextureBytes < requiredBytes)
		{
			XBLF("JA: fakegl registered linear upload truncated tex=%d size=%dx%d format=0x%08x row=%u pitch=%u required=%u registered=%u\n",
				m_textures.GetCurrentID(), width, height,
				(unsigned int)destPixelFormat,
				(unsigned int)rowBytes,
				(unsigned int)compatablePixelsPitch,
				(unsigned int)requiredBytes,
				(unsigned int)registeredTextureBytes);
			return false;
		}

		XGSwizzleRect(compatablePixels, compatablePixelsPitch, NULL,
			registeredTextureData, (DWORD)width, (DWORD)height, NULL,
			bytesPerPixel);

		{
			static int s_registeredLinearLogs = 0;
			if (s_registeredLinearLogs < 96)
			{
				const unsigned char *srcBytes = (const unsigned char *)compatablePixels;
				XBLF("EF: TEXUPLOAD_REGISTERED_LINEAR tex=%d image='%s' size=%ux%u format=0x%08x bpp=%d pitch=%u bytes=%u src0=%02x,%02x,%02x,%02x",
					m_textures.GetCurrentID(),
					FakeGL_CurrentTextureDebugName(),
					(unsigned int)width,
					(unsigned int)height,
					(unsigned int)destPixelFormat,
					bytesPerPixel,
					(unsigned int)compatablePixelsPitch,
					(unsigned int)requiredBytes,
					srcBytes ? srcBytes[0] : 0,
					srcBytes ? srcBytes[1] : 0,
					srcBytes ? srcBytes[2] : 0,
					srcBytes ? srcBytes[3] : 0);
				++s_registeredLinearLogs;
			}
		}
		return true;
	}
#endif

	void UploadFallbackTexture(IDirect3DTexture8* texture)
	{
		WORD pixels[8 * 8];
		for ( int y = 0; y < 8; ++y )
		{
			for ( int x = 0; x < 8; ++x )
			{
				pixels[y * 8 + x] = ((x ^ y) & 1) ? 0xffff : 0xf0ff;
			}
		}

		D3DLOCKED_RECT rect;
		HRESULT hr = texture->LockRect(0, &rect, NULL, 0);
		if ( FAILED(hr) )
		{
			InterpretError(hr);
			return;
		}

		XGSwizzleRect(pixels, 8 * sizeof(WORD), NULL, rect.pBits, 8, 8, NULL, sizeof(WORD));
		texture->UnlockRect(0);
	}

	bool EnsureFallbackTexture(void)
	{
		if (!m_pD3DDev)
		{
			return false;
		}

		if (!m_fallbackTexture)
		{
			HRESULT fallbackHr = CreateXboxTexture(8, 8, 1, 0, D3DFMT_A4R4G4B4, &m_fallbackTexture);
			if (FAILED(fallbackHr) || !m_fallbackTexture)
			{
#ifdef _XBOX
				XBLF("JA: fakegl fallback texture prewarm failed hr=0x%08x\n", (unsigned int)fallbackHr);
#endif
				return false;
			}
			UploadFallbackTexture(m_fallbackTexture);
#ifdef _XBOX
			TrackTextureAlloc("fallback-shared", EstimateTextureBytes(D3DFMT_A4R4G4B4, 8, 8, 1));
			XBL("JA: fakegl fallback texture ready\n");
#endif
		}
		return true;
	}

	bool UseFallbackTexture(D3DFORMAT d3dFormat, GLint internalFormat)
	{
		if (!EnsureFallbackTexture())
		{
			return false;
		}

		m_fallbackTexture->AddRef();
		m_textures.SetTexture(m_fallbackTexture, D3DFMT_A4R4G4B4, GL_RGBA4);
		m_textureState.DirtyTexture(m_textures.GetCurrentID());
		return true;
	}

public:
	void DeleteTexture(GLuint id)
	{
		m_textures.DeleteTexture(id);
		m_textureState.DirtyTexture(id);
#ifdef _XBOX
		static int s_deleteLogs = 0;
		if (s_deleteLogs < 64)
		{
			XBLF("JA: fakegl DeleteTexture tex=%u\n", id);
			++s_deleteLogs;
		}
#endif
	}

	bool IsTexture(GLuint id)
	{
		return m_textures.IsTexture(id);
	}

	FakeGL(/*HWND hwndMain*/)
	{
		//m_hwndMain = hwndMain;

		m_windowHeight = 480; //FIXME
		m_bD3DXReady = TRUE;

		m_pD3DDev = 0;
		m_pD3D = 0;
		m_pPrimary = 0;
		m_fallbackTexture = 0;
		m_hardwareTandL = false;
		m_modelViewMatrixStack = 0;
		m_projectionMatrixStack = 0;
		m_textureMatrixStack = 0;
		m_currentMatrixStack = 0;
		m_stickyAlloc = 0;
		m_stickyAllocSize = 0;
		m_subImageScratch = 0;
		m_subImageScratchSize = 0;
#ifdef _XBOX
		m_stefxEliteForceScriptPanelDraw = false;
#endif

		m_glRenderStateDirty = true;

		m_glAlphaStateDirty = true;
		m_glAlphaFunc = GL_ALWAYS;
		m_glAlphaFuncRef = 0;
		m_glAlphaTest = false;

		m_glBlendStateDirty = true;
		m_glBlend = false;
		m_glBlendFuncSFactor = GL_ONE; // Not sure this is the default
		m_glBlendFuncDFactor = GL_ZERO; // Not sure this is the default
	
		m_glCullStateDirty = true;
		m_glCullFace = false;

		m_glCullFaceMode = GL_BACK;
		
		m_glDepthStateDirty = true;
		m_glDepthTest = false;
		m_glDepthMask = true;
		m_glDepthFunc = GL_ALWAYS; // not sure if this is the default
		m_glClipPlane0StateDirty = true;
		m_glClipPlane0Enabled = false;
		m_glClipPlane0[0] = 0.0f;
		m_glClipPlane0[1] = 0.0f;
		m_glClipPlane0[2] = 0.0f;
		m_glClipPlane0[3] = 0.0f;
		m_glScissorTest = false;
		m_glScissorRect.x1 = 0;
		m_glScissorRect.y1 = 0;
		m_glScissorRect.x2 = (LONG)gWidth;
		m_glScissorRect.y2 = (LONG)gHeight;
		m_glFogStateDirty = true;
		m_glFog = false;
		m_glFogMode = GL_EXP;
		m_glFogDensity = 1.0f;
		m_glFogStart = 0.0f;
		m_glFogEnd = 1.0f;
		m_glFogColor = D3DCOLOR_ARGB(0, 0, 0, 0);

		m_glDepthRangeNear = 0; // not sure if this is the default
		m_glDepthRangeFar = 1.0; // not sure if this is the default

		m_glMatrixMode = GL_MODELVIEW; // Not sure this is the default

		m_glPolygonModeFront = GL_FILL;
		m_glPolygonModeBack = GL_FILL;

		m_glShadeModelStateDirty = true;
		m_glShadeModel = GL_SMOOTH;

		m_bViewPortDirty = true;
		m_glViewPortX = 0;
		m_glViewPortY = 0;
							
		m_glViewPortWidth = gWidth;//640;//Marty FIXME
		m_glViewPortHeight = gHeight;//480;
		m_glClearColor = D3DRGBA(0, 0, 0, 0);

		m_vendor = 0;
		m_renderer = 0;
		m_extensions = 0;

		m_hintGenerateMipMaps = true;
		
		InitD3DX();
		
		D3DXCreateMatrixStack(0, &m_modelViewMatrixStack);
		D3DXCreateMatrixStack(0, &m_projectionMatrixStack);
		D3DXCreateMatrixStack(0, &m_textureMatrixStack);
		m_currentMatrixStack = m_modelViewMatrixStack;
		m_modelViewMatrixStack->LoadIdentity(); // Not sure this is correct
		m_projectionMatrixStack->LoadIdentity();
		m_textureMatrixStack->LoadIdentity();
		m_modelViewMatrixStateDirty = true;
		m_projectionMatrixStateDirty = true;
		m_textureMatrixStateDirty = true;
		m_currentMatrixStateDirty = &m_modelViewMatrixStateDirty;
		m_viewMatrixStateDirty = true;

		D3DXMatrixIdentity(&m_d3dViewMatrix);

		m_needBeginScene = true;

		{
			// Check for multitexture.
			D3DCAPS8 deviceCaps;
			HRESULT hr = m_pD3DDev->GetDeviceCaps(&deviceCaps);
			if ( ! FAILED(hr)) 
			{
				// Clamp texture blend stages to 2. Some cards can do eight, but that's more
				// than we need.
				int maxStages = deviceCaps.MaxTextureBlendStages;
				if ( maxStages > 2 )
				{
					maxStages = 2;
				}
				m_textureState.SetMaxStages(maxStages);

				m_hardwareTandL = (deviceCaps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) != 0;
			}
		}

		// One-time render state initialization
		m_pD3DDev->SetRenderState( D3DRS_TEXTUREFACTOR, 0x00000000 );
		m_pD3DDev->SetRenderState( D3DRS_DITHERENABLE, 0 ); //FALSE looks worse in 16 bit mode (D3DFMT_X1R5G5B5)
		m_pD3DDev->SetRenderState( D3DRS_SPECULARENABLE, FALSE );
		m_pD3DDev->SetRenderState( D3DRS_LIGHTING, FALSE);
		m_pD3DDev->SetRenderState( D3DRS_FOGENABLE, FALSE);
	}
	~FakeGL()
	{
		delete [] m_stickyAlloc;
		delete [] m_subImageScratch;
		RELEASENULL(m_fallbackTexture);
		ReleaseD3DX();
		RELEASENULL(m_modelViewMatrixStack);
		RELEASENULL(m_projectionMatrixStack);
		RELEASENULL(m_textureMatrixStack);
	}

	void glAlphaFunc (GLenum func, GLclampf ref)
	{
		if ( m_glAlphaFunc != func || m_glAlphaFuncRef != ref )
		{
			m_glAlphaFunc = func;
			m_glAlphaFuncRef = ref;
		}
		// Q3 re-sends draw-critical state on Xbox because some renderer paths
		// touch D3D directly. Refresh D3D even when fakegl's logical cache matches.
		SetRenderStateDirty();
		m_glAlphaStateDirty = true;
	}
	
	void glBegin (GLenum mode)
	{
		if ( m_needBeginScene )
		{
			m_needBeginScene = false;
			HRESULT hr = m_pD3DDev->BeginScene();
			if ( FAILED(hr) )
			{
				InterpretError(hr);
			}
		}

#if 0
		// statistics
		static int beginCount;
		static int stateChangeCount;
		static int primitivesCount;
		beginCount++;
		if ( m_glRenderStateDirty )
			stateChangeCount++;
		if ( m_glRenderStateDirty || ! m_OGLPrimitiveVertexBuffer.IsMergableMode(mode) )
			primitivesCount++;
#endif
		if ( m_glRenderStateDirty || ! m_OGLPrimitiveVertexBuffer.IsMergableMode(mode) ) 
		{
#ifdef _XBOX
			{
				static int s_xboxBeginStateLogCount = 0;
				static int s_xboxBeginStage1LogCount = 0;
				const bool beginStage1Active = m_textureState.GetStageTexture2D(1);
				if (s_xboxBeginStateLogCount < 16 || (beginStage1Active && s_xboxBeginStage1LogCount < 24))
				{
					XBLF("JA: fakegl glBegin state mode=0x%08x dirty=%d textureDirty=%d mergable=%d maxStages=%d currentStage=%d stage0 dirty=%d tex=%u enabled=%d env=0x%08x stage1 dirty=%d tex=%u enabled=%d env=0x%08x",
						(unsigned int)mode,
						m_glRenderStateDirty ? 1 : 0,
						m_textureState.GetDirty() ? 1 : 0,
						m_OGLPrimitiveVertexBuffer.IsMergableMode(mode) ? 1 : 0,
						m_textureState.GetMaxStages(),
						m_textureState.GetCurrentStage(),
						m_textureState.GetStageDirty(0) ? 1 : 0,
						(unsigned int)m_textureState.GetStageTexture(0),
						m_textureState.GetStageTexture2D(0) ? 1 : 0,
						(unsigned int)m_textureState.GetStageTextEnvMode(0),
						m_textureState.GetStageDirty(1) ? 1 : 0,
						(unsigned int)m_textureState.GetStageTexture(1),
						m_textureState.GetStageTexture2D(1) ? 1 : 0,
						(unsigned int)m_textureState.GetStageTextEnvMode(1));
					++s_xboxBeginStateLogCount;
					if (beginStage1Active)
					{
						++s_xboxBeginStage1LogCount;
					}
				}
			}
#endif
			g_SPXBFakeGLStateFlushes++;
			internalEnd();
			SetGLRenderState();
			DWORD typeDesc;
			typeDesc = D3DFVF_XYZ | D3DFVF_DIFFUSE;
			int vertexTextureStages = 0;
			if (m_textureState.GetStageTexture2D(0))
			{
				vertexTextureStages = 1;
			}
			if (m_textureState.GetStageTexture2D(1))
			{
				vertexTextureStages = 2;
			}
			typeDesc |= (vertexTextureStages << D3DFVF_TEXCOUNT_SHIFT);

			if ( typeDesc != m_OGLPrimitiveVertexBuffer.GetVertexTypeDesc()
				|| !m_OGLPrimitiveVertexBuffer.HasDevice()) 
			{
				m_OGLPrimitiveVertexBuffer.Initialize(m_pD3DDev, m_pD3D, m_hardwareTandL, typeDesc);
			}
			m_OGLPrimitiveVertexBuffer.Begin(mode);
		}
		else
		{
			m_OGLPrimitiveVertexBuffer.Append(mode);
		}
	}

#ifdef _XBOX
	void SetEliteForceScriptPanelDrawContext(int active)
	{
		m_stefxEliteForceScriptPanelDraw = active != 0;
	}

	bool DrawIndexedPrimitiveUPXbox(D3DPRIMITIVETYPE dptPrimitiveType, DWORD typeDesc,
		UINT vertexCount, UINT primitiveCount, const void *indices,
		const void *vertices, UINT stride, int stefxOverlayActive,
		int stefxOverlayHud, int stefxOverlayBeam)
	{
		const bool stefxLateIndexed = false;

		if (!m_pD3DDev || !indices || !vertices || vertexCount == 0 || primitiveCount == 0 || stride == 0)
		{
			return false;
		}

		if ( m_needBeginScene )
		{
			m_needBeginScene = false;
			HRESULT hrBeginScene = m_pD3DDev->BeginScene();
			if ( FAILED(hrBeginScene) )
			{
				InterpretError(hrBeginScene);
				return false;
			}
		}

		/* Keep ordering correct with any pending immediate vertices, but avoid
		 * routing indexed array draws through an empty glBegin/glEnd pair. */
		internalEnd();
		if ( m_glRenderStateDirty )
		{
			g_SPXBFakeGLStateFlushes++;
			SetGLRenderState();
		}

		if (stefxOverlayActive)
		{
			static int s_stefxOverlayDeviceStateBudget = 32;
			const GLuint texture0 = m_textureState.GetStageTexture(0);
			TextureEntry *textureEntry0 = m_textures.GetEntry(texture0);

			/* This is the SP renderer's overlay state block, applied through the
			 * fake-GL device that owns the actual Xbox draw submission. */
			m_pD3DDev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
			m_pD3DDev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
			m_pD3DDev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
			m_pD3DDev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
			m_pD3DDev->SetRenderState(D3DRS_SRCBLEND,
				stefxOverlayBeam ? D3DBLEND_ONE : D3DBLEND_SRCALPHA);
			m_pD3DDev->SetRenderState(D3DRS_DESTBLEND,
				stefxOverlayBeam ? D3DBLEND_ONE : D3DBLEND_INVSRCALPHA);
			m_pD3DDev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
			m_pD3DDev->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
			m_pD3DDev->SetRenderState(D3DRS_BACKFILLMODE, D3DFILL_SOLID);

			if (s_stefxOverlayDeviceStateBudget > 0)
			{
				XBLF("STEFX_OVERLAY_DEVICE_STATE hud=%d beam=%d blend=%d src=0x%08x dst=0x%08x depth=%d depthMask=%d alphaTest=%d textureDirty=%d stage0Dirty=%d stage0Enabled=%d stage0Tex=%u stage0Env=0x%08x entry=%p mip=%p format=0x%08x internal=0x%08x",
					stefxOverlayHud,
					stefxOverlayBeam,
					m_glBlend ? 1 : 0,
					(unsigned int)m_glBlendFuncSFactor,
					(unsigned int)m_glBlendFuncDFactor,
					m_glDepthTest ? 1 : 0,
					m_glDepthMask ? 1 : 0,
					m_glAlphaTest ? 1 : 0,
					m_textureState.GetDirty() ? 1 : 0,
					m_textureState.GetStageDirty(0) ? 1 : 0,
					m_textureState.GetStageTexture2D(0) ? 1 : 0,
					(unsigned int)texture0,
					(unsigned int)m_textureState.GetStageTextEnvMode(0),
					textureEntry0,
					textureEntry0 ? textureEntry0->m_mipMap : NULL,
					textureEntry0 ? (unsigned int)textureEntry0->m_format : 0,
					textureEntry0 ? (unsigned int)textureEntry0->m_internalFormat : 0);
				--s_stefxOverlayDeviceStateBudget;
			}
		}

		if (stefxLateIndexed)
		{
			XBLF("STEFX: LATE fakegl DrawIndexedPrimitiveUP pre frame=%d type=%d prims=%u verts=%u indices=%p vtx=%p stride=%u vtxBytes=%lu idxBytes=%lu fvf=0x%08lx",
				g_stefxFakeglSwapFrame, (int)dptPrimitiveType,
				(unsigned int)primitiveCount, (unsigned int)vertexCount,
				indices, vertices, (unsigned int)stride,
				(unsigned long)(vertexCount * stride),
				(unsigned long)(primitiveCount * 3 * sizeof(unsigned short)),
				(unsigned long)typeDesc);
		}
		if (stefxLateIndexed)
		{
			XBLF("STEFX: LATE fakegl DrawIndexedPrimitiveUP before SetVertexShader frame=%d", g_stefxFakeglSwapFrame);
		}
		HRESULT hrSetVertexShader = m_pD3DDev->SetVertexShader(typeDesc);
		if (stefxLateIndexed)
		{
			XBLF("STEFX: LATE fakegl DrawIndexedPrimitiveUP SetVertexShader hr=0x%08lx frame=%d",
				(unsigned long)hrSetVertexShader, g_stefxFakeglSwapFrame);
		}
		if ( FAILED(hrSetVertexShader) )
		{
			static int s_xboxIndexedSetShaderFailBudget = 8;
			if (s_xboxIndexedSetShaderFailBudget > 0)
			{
				XBLF("JA: fakegl indexed SetVertexShader failed hr=0x%08lx fvf=0x%08lx",
					(unsigned long)hrSetVertexShader, (unsigned long)typeDesc);
				--s_xboxIndexedSetShaderFailBudget;
			}
			return false;
		}

		if (m_stefxEliteForceScriptPanelDraw)
		{
			static int s_stefxPanelFakeglApplyBudget = 160;
			const float fZOffset = -1.0f;
			const float fZSlopeScale = -1.0f;
			m_pD3DDev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
			m_pD3DDev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
			m_pD3DDev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
			m_pD3DDev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
			m_pD3DDev->SetRenderState(D3DRS_SOLIDOFFSETENABLE, TRUE);
			m_pD3DDev->SetRenderState(D3DRS_POLYGONOFFSETZOFFSET, *((const DWORD*)&fZOffset));
			m_pD3DDev->SetRenderState(D3DRS_POLYGONOFFSETZSLOPESCALE, *((const DWORD*)&fZSlopeScale));
			if (s_stefxPanelFakeglApplyBudget > 0)
			{
				XBLF("STEFX_SCRIPT_PANEL_FAKEGL_APPLY prims=%u verts=%u stride=%u fvf=0x%08lx zEnable=0 zWrite=0 zOffset=%g zSlope=%g",
					(unsigned int)primitiveCount,
					(unsigned int)vertexCount,
					(unsigned int)stride,
					(unsigned long)typeDesc,
					fZOffset,
					fZSlopeScale);
				--s_stefxPanelFakeglApplyBudget;
			}
		}

		if (stefxLateIndexed)
		{
			XBLF("STEFX: LATE fakegl DrawIndexedPrimitiveUP before draw frame=%d", g_stefxFakeglSwapFrame);
		}
		HRESULT hrDrawPrimitive = m_pD3DDev->DrawIndexedPrimitiveUP(
			dptPrimitiveType,
			0,
			vertexCount,
			primitiveCount,
			indices,
			D3DFMT_INDEX16,
			vertices,
			stride);
		if (stefxLateIndexed)
		{
			XBLF("STEFX: LATE fakegl DrawIndexedPrimitiveUP draw hr=0x%08lx frame=%d",
				(unsigned long)hrDrawPrimitive, g_stefxFakeglSwapFrame);
		}
		if ( FAILED(hrDrawPrimitive) )
		{
			static int s_xboxIndexedDrawFailBudget = 8;
			if (s_xboxIndexedDrawFailBudget > 0)
			{
				XBLF("JA: fakegl indexed DrawIndexedPrimitiveUP failed hr=0x%08lx prim=%lu verts=%u count=%u fvf=0x%08lx stride=%u",
					(unsigned long)hrDrawPrimitive, (unsigned long)dptPrimitiveType,
					(unsigned int)vertexCount, (unsigned int)primitiveCount,
					(unsigned long)typeDesc, (unsigned int)stride);
				--s_xboxIndexedDrawFailBudget;
			}
			return false;
		}

		if (m_stefxEliteForceScriptPanelDraw)
		{
			static int s_stefxPanelFakeglResetBudget = 80;
			m_pD3DDev->SetRenderState(D3DRS_SOLIDOFFSETENABLE, FALSE);
			if (s_stefxPanelFakeglResetBudget > 0)
			{
				XBLF("STEFX_SCRIPT_PANEL_FAKEGL_RESET prims=%u verts=%u",
					(unsigned int)primitiveCount,
					(unsigned int)vertexCount);
				--s_stefxPanelFakeglResetBudget;
			}
		}

		g_SPXBFakeGLPrimitiveCalls++;
		g_SPXBFakeGLPrimitiveVerts += vertexCount;
		return true;
	}
#endif

	void glBindTexture(GLenum target, GLuint texture)
	{
		if ( target != GL_TEXTURE_2D ) 
		{
			LocalDebugBreak();
			return;
		}
		if ( m_textureState.GetCurrentTexture() != texture ) 
		{
#ifdef _XBOX
			{
				static int s_xboxBindTextureLogCount = 0;
				if (s_xboxBindTextureLogCount < 32 || (m_textureState.GetCurrentStage() == 1 && s_xboxBindTextureLogCount < 64))
				{
					XBLF("JA: fakegl glBindTexture stage=%d old=%u new=%u target=0x%08x",
						m_textureState.GetCurrentStage(),
						(unsigned int)m_textureState.GetCurrentTexture(),
						(unsigned int)texture,
						(unsigned int)target);
					++s_xboxBindTextureLogCount;
				}
			}
#endif
			SetRenderStateDirty();
			m_textureState.SetCurrentTexture(texture);
			m_textures.BindTexture(texture);
		}
#ifdef _XBOX
		else
		{
			const int stage = m_textureState.GetCurrentStage();
			if (stage >= 0 && stage < 2)
			{
				static int s_stefxSameTextureRebindLogCount = 0;
				SetRenderStateDirty();
				m_textureState.ForceStageDirty(stage);
				m_textures.BindTexture(texture);
				if (s_stefxSameTextureRebindLogCount < 32)
				{
					XBLF("STEFX: fakegl rebind same texture dirty stage=%d tex=%u enabled=%d",
						stage,
						(unsigned int)texture,
						m_textureState.GetTexture2D() ? 1 : 0);
					++s_stefxSameTextureRebindLogCount;
				}
			}
		}
#endif
	}

	inline void glMTexCoord2fSGIS(GLenum target, GLfloat s, GLfloat t)
	{
		int textStage = target - TEXTURE0_SGIS;
		m_OGLPrimitiveVertexBuffer.SetTextureCoord(textStage, s, t);
	}
	
	inline void glSelectTextureSGIS(GLenum target)
	{
		int textStage = target - TEXTURE0_SGIS;
		m_textureState.SetCurrentStage(textStage);
		m_textures.BindTexture(m_textureState.GetCurrentTexture());
#ifdef _XBOX
		{
			static int s_xboxSelectTextureLogCount = 0;
			if (s_xboxSelectTextureLogCount < 32 || (textStage == 1 && s_xboxSelectTextureLogCount < 64))
			{
				XBLF("JA: fakegl select texture target=0x%08x stage=%d currentTex=%u enabled=%d",
					(unsigned int)target,
					textStage,
					(unsigned int)m_textureState.GetCurrentTexture(),
					m_textureState.GetTexture2D() ? 1 : 0);
				++s_xboxSelectTextureLogCount;
			}
		}
#endif
		// Does not, by itself, dirty the render state
	}

	void glBlendFunc (GLenum sfactor, GLenum dfactor)
	{
		if ( m_glBlendFuncSFactor != sfactor || m_glBlendFuncDFactor != dfactor ) 
		{
			m_glBlendFuncSFactor = sfactor;
			m_glBlendFuncDFactor = dfactor;
		}
		SetRenderStateDirty();
		m_glBlendStateDirty = true;
	}

	inline void glClear (GLbitfield mask)
	{
		internalEnd();
		SetGLRenderState();
		DWORD clearMask = 0;

		if ( mask & GL_COLOR_BUFFER_BIT )
			clearMask |= D3DCLEAR_TARGET;

		if ( mask & GL_DEPTH_BUFFER_BIT ) 
			clearMask |= D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL; 

		// see Depth-Buffer Compression and Performance Implications in the docs!
		// Quake does not use a stencil buffer, but we need to clear it anyways else
		// the performance will go down

		if ( mask & GL_STENCIL_BUFFER_BIT )
			clearMask |= D3DCLEAR_STENCIL;

		const D3DRECT *clearRects = NULL;
		DWORD clearRectCount = 0;
		if (m_glScissorTest &&
			m_glScissorRect.x2 > m_glScissorRect.x1 &&
			m_glScissorRect.y2 > m_glScissorRect.y1)
		{
			clearRects = &m_glScissorRect;
			clearRectCount = 1;
		}
#ifdef _XBOX
		{
			static int s_xboxClearLogCount = 0;
			if (s_xboxClearLogCount < 24)
			{
				XBLF("JA: fakegl Clear mask=0x%08lx rects=%lu scissor=%d rect=%ld,%ld,%ld,%ld",
					(unsigned long)clearMask,
					(unsigned long)clearRectCount,
					m_glScissorTest ? 1 : 0,
					m_glScissorRect.x1,
					m_glScissorRect.y1,
					m_glScissorRect.x2,
					m_glScissorRect.y2);
			}
			++s_xboxClearLogCount;
		}
#endif
		m_pD3DDev->Clear(clearRectCount, clearRects, clearMask, m_glClearColor, 1.0f, 0 );
	}

	void glClearColor (GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
	{
		m_glClearColor = D3DRGBA(Clamp(red), Clamp(green), Clamp(blue), Clamp(alpha));
	}

	inline void glColor3f (GLfloat red, GLfloat green, GLfloat blue)
	{
		// Note: On x86 architectures this function will chew up a lot of time
		// converting floating point to integer by calling _ftol
		// unless the /QIfist flag is specified.
		m_OGLPrimitiveVertexBuffer.SetColor(D3DRGB(Clamp(red), Clamp(green), Clamp(blue)));
	}

	inline void glColor3ubv (const GLubyte *v)
	{
		m_OGLPrimitiveVertexBuffer.SetColor(RGBA_MAKE(v[0], v[1], v[2], 0xff));
	}

	inline void glColor4ubv (const GLubyte *v)
	{
		m_OGLPrimitiveVertexBuffer.SetColor(RGBA_MAKE(v[0], v[1], v[2], v[3]));
	}

	inline void glColor4f (GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
	{
		// Note: On x86 architectures this function will chew up a lot of time
		// converting floating point to integer by calling _ftol
		// unless the /QIfist flag is specified.
		m_OGLPrimitiveVertexBuffer.SetColor(D3DRGBA(Clamp(red), Clamp(green), Clamp(blue), Clamp(alpha)));
	}

	inline void glColor4fv (const GLfloat *v)
	{
		// Note: On x86 architectures this function will chew up a lot of time
		// converting floating point to integer by calling _ftol
		// unless the /QIfist flag is specified.
		m_OGLPrimitiveVertexBuffer.SetColor(D3DRGBA(Clamp(v[0]), Clamp(v[1]), Clamp(v[2]), Clamp(v[3])));
	}

	inline void glColor4ub (GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha)
	{
		m_OGLPrimitiveVertexBuffer.SetColor(RGBA_MAKE(red, green, blue, alpha));
	}

	void glClipPlane0( const GLdouble *equation )
	{
		if ( equation )
		{
			m_glClipPlane0[0] = (float)equation[0];
			m_glClipPlane0[1] = (float)equation[1];
			m_glClipPlane0[2] = (float)equation[2];
			m_glClipPlane0[3] = (float)equation[3];
			m_glClipPlane0StateDirty = true;
			SetRenderStateDirty();
#ifdef _XBOX
			static int s_clipPlaneSetLogCount = 0;
			if (s_clipPlaneSetLogCount < 16)
			{
				XBLF("JA: fakegl GL_CLIP_PLANE0 plane=%g,%g,%g,%g",
					m_glClipPlane0[0], m_glClipPlane0[1],
					m_glClipPlane0[2], m_glClipPlane0[3]);
			}
			++s_clipPlaneSetLogCount;
#endif
		}
	}

	void glScissor(GLint x, GLint y, GLsizei width, GLsizei height)
	{
		if (x < 0) x = 0;
		else if ((DWORD)x > gWidth) x = (GLint)gWidth;
		if (y < 0) y = 0;
		else if ((DWORD)y > gHeight) y = (GLint)gHeight;
		if (width < 0) width = 0;
		else if ((DWORD)(x + width) > gWidth) width = (GLsizei)(gWidth - x);
		if (height < 0) height = 0;
		else if ((DWORD)(y + height) > gHeight) height = (GLsizei)(gHeight - y);

		y = (GLint)gHeight - (y + height);
		m_glScissorRect.x1 = x;
		m_glScissorRect.y1 = y;
		m_glScissorRect.x2 = x + width;
		m_glScissorRect.y2 = y + height;

		if (m_pD3DDev && m_glScissorTest)
		{
			m_pD3DDev->SetScissors(1, FALSE, &m_glScissorRect);
		}
#ifdef _XBOX
		{
			static int s_scissorLogCount = 0;
			if (s_scissorLogCount < 16)
			{
				XBLF("JA: fakegl Scissor enabled=%d rect=%ld,%ld,%ld,%ld",
					m_glScissorTest ? 1 : 0,
					m_glScissorRect.x1, m_glScissorRect.y1,
					m_glScissorRect.x2, m_glScissorRect.y2);
			}
			++s_scissorLogCount;
		}
#endif
	}

	void glCullFace (GLenum mode)
	{
		if ( m_glCullFaceMode != mode ) 
		{
			m_glCullFaceMode = mode;
		}
		SetRenderStateDirty();
		m_glCullStateDirty = true;
	}

	void glDepthFunc (GLenum func)
	{
		if ( m_glDepthFunc != func ) 
		{
			m_glDepthFunc = func;
		}
		SetRenderStateDirty();
		m_glDepthStateDirty = true;
	}

	void glDepthMask (GLboolean flag)
	{
		if ( m_glDepthMask != (flag != 0) ) 
		{
			m_glDepthMask = flag != 0 ? true : false;
		}
		SetRenderStateDirty();
		m_glDepthStateDirty = true;
	}

	void glDepthRange (GLclampd zNear, GLclampd zFar)
	{
		if ( m_glDepthRangeNear != zNear || m_glDepthRangeFar != zFar ) 
		{
			SetRenderStateDirty();
			m_glDepthRangeNear = zNear;
			m_glDepthRangeFar = zFar;
			m_bViewPortDirty = true;
		}
	}

	void glDisable (GLenum cap)
	{
		glEnableDisableSet(cap, false);
	}

	void glDrawBuffer (GLenum /* mode */)
	{
		// Do nothing. (Can DirectX render to the front buffer at all?)
	}

	void glEnable (GLenum cap)
	{
		glEnableDisableSet(cap, true); 
	}

	void glEnableDisableSet(GLenum cap, bool value)
	{
		switch ( cap ) 
		{
		case GL_ALPHA_TEST:
			if ( m_glAlphaTest != value ) 
			{
				m_glAlphaTest = value;
			}
			SetRenderStateDirty();
			m_glAlphaStateDirty = true;
			break;
		case GL_BLEND:
			if ( m_glBlend != value )
			{
				m_textureState.SetMainBlend(value); 
				m_glBlend = value;
			}
			SetRenderStateDirty();
			m_glBlendStateDirty = true;
			break;
		case GL_CULL_FACE:
			if ( m_glCullFace != value )
			{
				m_glCullFace = value;
			}
			SetRenderStateDirty();
			m_glCullStateDirty = true;
			break;
		case GL_DEPTH_TEST:
			if ( m_glDepthTest != value ) 
			{
				m_glDepthTest = value;
			}
			SetRenderStateDirty();
			m_glDepthStateDirty = true;
			break;
		case GL_TEXTURE_2D:
			if ( m_textureState.GetTexture2D() != value )
			{
#ifdef _XBOX
				{
					static int s_xboxTexture2DLogCount = 0;
					if (s_xboxTexture2DLogCount < 16 || (m_textureState.GetCurrentStage() == 1 && s_xboxTexture2DLogCount < 32))
					{
						XBLF("JA: fakegl texture2d stage=%d value=%d old=%d tex=%u",
							m_textureState.GetCurrentStage(),
							value ? 1 : 0,
							m_textureState.GetTexture2D() ? 1 : 0,
							(unsigned int)m_textureState.GetCurrentTexture());
						++s_xboxTexture2DLogCount;
					}
				}
#endif
				SetRenderStateDirty();
				m_textureState.SetTexture2D(value);
			}
			break;
		case GL_LIGHTING:
		case GL_POLYGON_OFFSET_FILL:
			// JKA's splash/loading path saves and restores these GL caps, but
			// fakegl does not implement them. Treat them as disabled no-ops.
			break;
		case GL_SCISSOR_TEST:
			m_glScissorTest = value;
			if (m_pD3DDev)
			{
				m_pD3DDev->SetScissors(value ? 1 : 0, FALSE, &m_glScissorRect);
			}
			break;
		case GL_FOG:
			if ( m_glFog != value )
			{
				SetRenderStateDirty();
				m_glFog = value;
				m_glFogStateDirty = true;
#ifdef _XBOX
				{
					static int s_fogEnableLogCount = 0;
					if (s_fogEnableLogCount < 24)
					{
						XBLF("JA: fakegl fog %s mode=0x%04x start=%g end=%g density=%g color=0x%08x",
							value ? "enable" : "disable",
							(unsigned int)m_glFogMode,
							(float)m_glFogStart,
							(float)m_glFogEnd,
							(float)m_glFogDensity,
							(unsigned int)m_glFogColor);
					}
					++s_fogEnableLogCount;
				}
#endif
			}
			break;
		case GL_STENCIL_TEST:
			break;
		case GL_CLIP_PLANE0:
			if ( m_glClipPlane0Enabled != value )
			{
				SetRenderStateDirty();
				m_glClipPlane0Enabled = value;
				m_glClipPlane0StateDirty = true;
			}
#ifdef _XBOX
			{
				static int s_clipPlaneLogCount = 0;
				if (s_clipPlaneLogCount < 16)
				{
					XBLF("JA: fakegl GL_CLIP_PLANE0 %s no-op on Xbox fixed-function path",
						value ? "enable" : "disable");
				}
				++s_clipPlaneLogCount;
			}
#endif
			break;
		default:
#ifdef _XBOX
			{
				static int s_unknownEnableDisableCount = 0;
				if (s_unknownEnableDisableCount < 8)
				{
					XBLF("JA: fakegl unsupported gl%s cap=0x%08x",
						value ? "Enable" : "Disable", (unsigned int)cap);
				}
				s_unknownEnableDisableCount++;
			}
#endif
			LocalDebugBreak();
			break;
		}
	}

	void glEnd (void)
	{
#ifdef _XBOX
		internalEnd();
#endif
	}

	void internalEnd()
	{
		if (m_OGLPrimitiveVertexBuffer.IsEmpty())
		{
			return;
		}
		m_OGLPrimitiveVertexBuffer.EnsureDevice(m_pD3DDev);
#ifdef _XBOX
		m_OGLPrimitiveVertexBuffer.End(m_glClipPlane0Enabled, m_glClipPlane0, m_modelViewMatrixStack->GetTop());
#else
		m_OGLPrimitiveVertexBuffer.End();
#endif
	}

	void glFinish (void)
	{
		// To Do: This is supposed to flush all pending commands
		internalEnd();
	}

	void glFogf(GLenum pname, GLfloat param)
	{
		bool changed = false;
		switch (pname)
		{
		case GL_FOG_DENSITY:
			if (m_glFogDensity == param)
			{
				return;
			}
			m_glFogDensity = param;
			changed = true;
			break;
		case GL_FOG_START:
			if (m_glFogStart == param)
			{
				return;
			}
			m_glFogStart = param;
			changed = true;
			break;
		case GL_FOG_END:
			if (m_glFogEnd == param)
			{
				return;
			}
			m_glFogEnd = param;
			changed = true;
			break;
		default:
			return;
		}
		if (!changed)
		{
			return;
		}
		SetRenderStateDirty();
		m_glFogStateDirty = true;
#ifdef _XBOX
		{
			static int s_fogfLogCount = 0;
			if (s_fogfLogCount < 24)
			{
				XBLF("JA: fakegl fogf pname=0x%04x param=%g", (unsigned int)pname, (float)param);
			}
			++s_fogfLogCount;
		}
#endif
	}

	void glFogfv(GLenum pname, const GLfloat *params)
	{
		if (!params || pname != GL_FOG_COLOR)
		{
			return;
		}
		int r = (int)(params[0] * 255.0f);
		int g = (int)(params[1] * 255.0f);
		int b = (int)(params[2] * 255.0f);
		if (r < 0) r = 0; else if (r > 255) r = 255;
		if (g < 0) g = 0; else if (g > 255) g = 255;
		if (b < 0) b = 0; else if (b > 255) b = 255;
		D3DCOLOR fogColor = D3DCOLOR_ARGB(0, r, g, b);
		if (m_glFogColor == fogColor)
		{
			return;
		}
		m_glFogColor = fogColor;
		SetRenderStateDirty();
		m_glFogStateDirty = true;
#ifdef _XBOX
		{
			static int s_fogfvLogCount = 0;
			if (s_fogfvLogCount < 24)
			{
				XBLF("JA: fakegl fog color %g,%g,%g -> 0x%08x",
					(float)params[0], (float)params[1], (float)params[2], (unsigned int)m_glFogColor);
			}
			++s_fogfvLogCount;
		}
#endif
	}

	void glFogi(GLenum pname, GLint param)
	{
		if (pname != GL_FOG_MODE)
		{
			return;
		}
		if (m_glFogMode == param)
		{
			return;
		}
		m_glFogMode = param;
		SetRenderStateDirty();
		m_glFogStateDirty = true;
#ifdef _XBOX
		{
			static int s_fogiLogCount = 0;
			if (s_fogiLogCount < 24)
			{
				XBLF("JA: fakegl fog mode=0x%04x", (unsigned int)param);
			}
			++s_fogiLogCount;
		}
#endif
	}
	
	void glFrustum (GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar)
	{
		SetRenderStateDirty();
		D3DXMATRIX m;
		// Note that D3D takes top, bottom arguments in opposite order
		D3DXMatrixPerspectiveOffCenterRH(&m, left, right, bottom, top, zNear, zFar);
		m_currentMatrixStack->MultMatrixLocal(&m);
		*m_currentMatrixStateDirty = true;
	}

	void glGetFloatv (GLenum pname, GLfloat *params)
	{
		switch(pname)
		{
		case GL_MODELVIEW_MATRIX:
			memcpy(params,m_modelViewMatrixStack->GetTop(), sizeof(D3DMATRIX));
			break;
		default:
			LocalDebugBreak();
			break;
		}
	}

	const GLubyte * glGetString (GLenum name)
	{
		const char* result = "";
		EnsureDriverInfo();
		switch ( name )
		{
		case GL_VENDOR:
			result = m_vendor;
			break;
		case GL_RENDERER:
			result = m_renderer;
			break;
		case GL_VERSION:
			result = m_version;
			break;
		case GL_EXTENSIONS:
			result = m_extensions;
			break;
		default:
			break;
		}
		return (const GLubyte *) result;
	}

	void glHint (GLenum /* target */, GLenum /* mode */)
	{
		LocalDebugBreak();
	}

	GLboolean glIsEnabled(GLenum cap)
	{
		switch(cap)
		{
		case GL_ALPHA_TEST:
			return  m_glAlphaTest ? 1 : 0;
		case GL_BLEND:
			return m_glBlend ? 1 : 0;
		case GL_CULL_FACE:
			return m_glCullFace ? 1 : 0;
		case GL_DEPTH_TEST:
			return m_glDepthTest ? 1 : 0;
		case GL_TEXTURE_2D:
			return m_textureState.GetTexture2D() ? 1 : 0;
		case GL_SCISSOR_TEST:
			return m_glScissorTest ? 1 : 0;
		case GL_FOG:
			return m_glFog ? 1 : 0;
		case GL_STENCIL_TEST:
		case GL_LIGHTING:
		case GL_POLYGON_OFFSET_FILL:
			return 0;
		default:
#ifdef _XBOX
			{
				static int s_unknownIsEnabledCount = 0;
				if (s_unknownIsEnabledCount < 8)
				{
					XBLF("JA: fakegl unsupported glIsEnabled cap=0x%08x", (unsigned int)cap);
				}
				s_unknownIsEnabledCount++;
			}
#endif
			return FALSE;
		}
	}

	void glLoadIdentity (void)
	{
		SetRenderStateDirty();
		m_currentMatrixStack->LoadIdentity();
		*m_currentMatrixStateDirty = true;
	}

	void glLoadMatrixf (const GLfloat *m)
	{
		SetRenderStateDirty();
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_MP)
		if (FakeGL_STEFXShouldTransposeMatrix(m_glMatrixMode))
		{
			D3DXMATRIX matrix;
			D3DXMatrixTranspose(&matrix, (const D3DXMATRIX*)m);
			m_currentMatrixStack->LoadMatrix(&matrix);
		}
		else
		{
			m_currentMatrixStack->LoadMatrix((D3DXMATRIX*) m);
		}
#else
		m_currentMatrixStack->LoadMatrix((D3DXMATRIX*) m);
#endif
		*m_currentMatrixStateDirty = true;
	}

	void glMatrixMode (GLenum mode)
	{
		m_glMatrixMode = mode;
		switch ( mode ) 
		{
		case GL_MODELVIEW:
			m_currentMatrixStack = m_modelViewMatrixStack;
			m_currentMatrixStateDirty = &m_modelViewMatrixStateDirty;
			break;
		case GL_PROJECTION:
			m_currentMatrixStack = m_projectionMatrixStack;
			m_currentMatrixStateDirty = &m_projectionMatrixStateDirty;
			break;
		case GL_TEXTURE:
			m_currentMatrixStack = m_textureMatrixStack;
			m_currentMatrixStateDirty = &m_textureMatrixStateDirty;
			break;
		default:
			LocalDebugBreak();
			break;
		}
	}

	void glOrtho (GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar)
	{
		SetRenderStateDirty();
		D3DXMATRIX m;
		D3DXMatrixOrthoOffCenterRH(&m, left, right, top, bottom, zNear, zFar);
		m_currentMatrixStack->MultMatrixLocal(&m);
		*m_currentMatrixStateDirty = true;
	}

	void glPolygonMode (GLenum face, GLenum mode)
	{
		SetRenderStateDirty();
		switch ( face )
		{
		case GL_FRONT:
			m_glPolygonModeFront = mode;
			break;
		case GL_BACK:
			m_glPolygonModeBack = mode;
			break;
		case GL_FRONT_AND_BACK:
			m_glPolygonModeFront = mode;
			m_glPolygonModeBack = mode;
			break;
		default:
			LocalDebugBreak();
			break;
		}
	}

	void glPopMatrix (void)
	{
		SetRenderStateDirty();
		m_currentMatrixStack->Pop();
		*m_currentMatrixStateDirty = true;
	}

	void glPushMatrix (void)
	{
		m_currentMatrixStack->Push();
		// Doesn't dirty matrix state
	}

	void glReadBuffer (GLenum /* mode */)
	{
		// Not that we allow reading from various buffers anyway.
	}

	void glReadPixels (GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels)
	{
		if ( format != GL_RGB || type != GL_UNSIGNED_BYTE) 
		{
			LocalDebugBreak();
			return;
		}
		internalEnd();

#if 0 // Temporarily disable, because I don't want to port DDSURFACEDESC2 to DX8
		if(back) 
		{
			DDSURFACEDESC2 desc = {sizeof(desc) };
			HRESULT hr = back->Lock(NULL, &desc, DDLOCK_READONLY | DDLOCK_WAIT, 0);
			if ( FAILED(hr) ) 
			{
				InterpretError(hr);
				return;
			}
			CopyBitsToRGB(pixels, x, y, width, height, &desc);
			back->Unlock(NULL);
			RELEASENULL(back);
		}
#endif
	}

	static WORD GetNumberOfBits( DWORD dwMask )
	{
		WORD wBits = 0;
		while( dwMask )
		{
			dwMask = dwMask & ( dwMask - 1 );  
			wBits++;
		}
		return wBits;
	}

	static WORD GetShift( DWORD dwMask )
	{
		for(WORD i = 0; i < 32; i++ ) {
			if ( (1 << i) & dwMask ) {
				return i;
			}
		}
		return 0; // no bits in mask.
	}

	// Extract the bits and replicate out to an eight bit value
	static DWORD ExtractAndNormalize(DWORD rgba, DWORD shift, DWORD bits, DWORD mask){
		DWORD v = (rgba & mask) >> shift;
		// Assume bits >= 4
		v = (v | (v << bits));
		v = v >> (bits*2 - 8);
		return v;
	}

#if 0 // Temporarily disable
	void CopyBitsToRGB(void* pixels, DWORD sx, DWORD sy, DWORD width, DWORD height, LPDDSURFACEDESC2 pDesc){
		if ( ! (pDesc->ddpfPixelFormat.dwFlags & DDPF_RGB) ) {
			return; // Can't handle non-RGB surfaces
		}
		// We have to flip the Y axis to convert from D3D to openGL
		long destEndOfLineSkip = -2 * (width * 3);
		unsigned char* pDest = ((unsigned char*) pixels) + (height - 1) * width * 3 ;
		switch ( pDesc->ddpfPixelFormat.dwRGBBitCount ) {
		default:
			return;
		case 16:
			{
				unsigned short* pSource = (unsigned short*)
					(((unsigned char*) pDesc->lpSurface) + sx * sizeof(unsigned short) + sy * pDesc->lPitch);
				DWORD endOfLineSkip = pDesc->lPitch / sizeof(unsigned short) - pDesc->dwWidth;
				DWORD rMask = pDesc->ddpfPixelFormat.dwRBitMask;
				DWORD gMask = pDesc->ddpfPixelFormat.dwGBitMask;
				DWORD bMask = pDesc->ddpfPixelFormat.dwBBitMask;
				DWORD rShift = GetShift(rMask);
				DWORD rBits = GetNumberOfBits(rMask);
				DWORD gShift = GetShift(gMask);
				DWORD gBits = GetNumberOfBits(gMask);
				DWORD bShift = GetShift(bMask);
				DWORD bBits = GetNumberOfBits(bMask);
				for(DWORD y = 0; y < height; y++ ) {
					for (DWORD x = 0; x < width; x++ ) {
						unsigned short rgba = *pSource++;
						*pDest++ = ExtractAndNormalize(rgba, rShift, rBits, rMask);
						*pDest++ = ExtractAndNormalize(rgba, gShift, gBits, gMask);
						*pDest++ = ExtractAndNormalize(rgba, bShift, bBits, bMask);
					}
					pSource += endOfLineSkip;
					pDest += destEndOfLineSkip;
				}
			}
			break;
		case 32:
			{
				unsigned long* pSource = (unsigned long*)
					(((unsigned char*) pDesc->lpSurface) + sx * sizeof(unsigned long) + sy * pDesc->lPitch);
				DWORD endOfLineSkip = pDesc->lPitch / sizeof(unsigned long) - pDesc->dwWidth;
				for(DWORD y = 0; y < height; y++ ) {
					for (DWORD x = 0; x < width; x++ ) {
						unsigned long rgba = *pSource++;
						*pDest++ = RGBA_GETRED(rgba);
						*pDest++ = RGBA_GETGREEN(rgba);
						*pDest++ = RGBA_GETBLUE(rgba);
					}
					pSource += endOfLineSkip;
					pDest += destEndOfLineSkip;
				}
			}
			break;
		}
	}

#endif // Temporarily disable

	inline void glRotatef (GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
	{
		SetRenderStateDirty();
		D3DXMATRIX m;
		D3DXVECTOR3 v;
		v.x = x;
		v.y = y;
		v.z = z;
		// GL uses counterclockwise degrees, DX uses clockwise radians
		float dxAngle = angle * 3.14159265359 / 180;
		m_currentMatrixStack->RotateAxisLocal(&v, dxAngle);
		*m_currentMatrixStateDirty = true;
	}

	inline void glScalef (GLfloat x, GLfloat y, GLfloat z)
	{
		SetRenderStateDirty();
		D3DXMATRIX m;
		D3DXMatrixScaling(&m, x, y, z);
		m_currentMatrixStack->MultMatrixLocal(&m);
		*m_currentMatrixStateDirty = true;
	}

	void glShadeModel (GLenum mode)
	{
		if ( m_glShadeModel != mode )
		{
			SetRenderStateDirty();
			m_glShadeModel = mode;
			m_glShadeModelStateDirty = true;
		}
	}

	inline void glTexCoord2f (GLfloat s, GLfloat t)
	{
		m_OGLPrimitiveVertexBuffer.SetTextureCoord0(s, t);
	}

	void glTexEnvf (GLenum /* target */, GLenum /* pname */, GLfloat param)
	{
		// Ignore target, which must be GL_TEXTURE_ENV
		// Ignore pname, which must be GL_TEXTURE_ENV_MODE
		if ( m_textureState.GetTextEnvMode() != param ) 
		{
#ifdef _XBOX
			{
				static int s_xboxTexEnvLogCount = 0;
				if (s_xboxTexEnvLogCount < 64 || (m_textureState.GetCurrentStage() == 1 && s_xboxTexEnvLogCount < 160))
				{
					XBLF("JA: fakegl texenv stage=%d old=0x%08x new=0x%08x tex=%u enabled=%d",
						m_textureState.GetCurrentStage(),
						(unsigned int)m_textureState.GetTextEnvMode(),
						(unsigned int)param,
						(unsigned int)m_textureState.GetCurrentTexture(),
						m_textureState.GetTexture2D() ? 1 : 0);
					++s_xboxTexEnvLogCount;
				}
			}
#endif
			SetRenderStateDirty();
			m_textureState.SetTextEnvMode(param);
		}
	}

	static int MipMapSize(DWORD width, DWORD height)
	{
		DWORD n = width < height? width : height;
		DWORD result = 1;
		while (n > (DWORD) (1 << result) ) 
		{
			result++;
		}
		return result;
	}

#define LOAD_OURSELVES

	void glTexImage2D (GLenum target, GLint level, GLint internalformat, GLsizei width,
		GLsizei height, GLint /* border */, GLenum format, GLenum type, const GLvoid *pixels)
	{
		HRESULT hr;
		if ( target != GL_TEXTURE_2D || type != GL_UNSIGNED_BYTE) 
		{
			InterpretError(E_FAIL);
			return;
		}

		bool isDynamic = format == GL_LUMINANCE; // Lightmaps use this format.

		DWORD dxWidth = width;
		DWORD dxHeight = height;

		D3DFORMAT srcPixelFormat = GLToDXPixelFormat(internalformat, format);
		D3DFORMAT destPixelFormat = srcPixelFormat;
		// Can the surface handle that format?
		hr = S_OK;
		if ( m_pD3D )
		{
			hr = m_pD3D->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, m_d3dsdBackBuffer.Format,
				0, D3DRTYPE_TEXTURE, destPixelFormat);
		}
		if ( FAILED(hr) ) 
		{
			if ( g_force16bitTextures ) 
			{
				destPixelFormat = D3DFMT_A4R4G4B4;
				hr = m_pD3D ? m_pD3D->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, m_d3dsdBackBuffer.Format,
					0, D3DRTYPE_TEXTURE, destPixelFormat) : S_OK;
				if ( FAILED(hr) ) 
				{
					// Don't know what to do.
					InterpretError(E_FAIL);
					return;
				}
			}
			else 
			{
				destPixelFormat = D3DFMT_A8R8G8B8;
				hr = m_pD3D ? m_pD3D->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, m_d3dsdBackBuffer.Format,
					0, D3DRTYPE_TEXTURE, destPixelFormat) : S_OK;
				if ( FAILED(hr) ) 
				{
					// The card can't handle this pixel format. Switch to D3DX_SF_A4R4G4B4
					destPixelFormat = D3DFMT_A4R4G4B4;
					hr = m_pD3D ? m_pD3D->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, m_d3dsdBackBuffer.Format,
						0, D3DRTYPE_TEXTURE, destPixelFormat) : S_OK;
					if ( FAILED(hr) )
					{
						// Don't know what to do.
						InterpretError(E_FAIL);
						return;
					}
				}
			}
		}

#ifdef LOAD_OURSELVES

		char* goodSizeBits = (char*) pixels;
		if ( dxWidth != (DWORD) width || dxHeight != (DWORD) height )
		{
			// Most likely this is because there is a 256 x 256 limit on the texture size.
			goodSizeBits = new char[sizeof(DWORD) * dxWidth * dxHeight]; 
			DWORD* dest = ((DWORD*) goodSizeBits);
			for ( DWORD y = 0; y < dxHeight; y++) 
			{
				DWORD sy = y * height / dxHeight;
				for(DWORD x = 0; x < dxWidth; x++) 
				{
					DWORD sx = x * width / dxWidth;
					DWORD* source = ((DWORD*) pixels) + sy * dxWidth + sx;
					*dest++ = *source;
				}
			}
			width = dxWidth;
			height = dxHeight;
		}
		// TODO: Convert the pixels on the fly while copying into the DX texture.
		char* compatablePixels;
		DWORD compatablePixelsPitch;

		hr = ConvertToCompatablePixels(internalformat, width, height, format,
				type, destPixelFormat, goodSizeBits, &compatablePixels, &compatablePixelsPitch);

		if ( goodSizeBits != pixels ) 
		{
			delete [] goodSizeBits;
		}
		if ( FAILED(hr))
		{
			InterpretError(hr);
			return;
		}

#endif

		IDirect3DTexture8* pMipMap = m_textures.GetMipMap();
		if ( pMipMap )
		{
			// DX8 textures don't know much. Always reset texture for level zero.
			if ( level == 0 ) 
			{
				m_textures.SetTexture(NULL, D3DFMT_UNKNOWN, 0);
				pMipMap = 0;
			}
			// For non-square textures, OpenGL uses more MIPMAP levels than DirectX does.
			else if ( level >= (int)pMipMap->GetLevelCount() ) 
			{
				return;
			}
		}

#ifdef _XBOX
		bool ownsTextureHeader = false;
		DWORD registeredTextureBytes = 0;
		void* registeredTextureData = NULL;
#endif

		if( ! pMipMap) 
		{
			int levels = 1;
#ifndef _XBOX
			if ( m_hintGenerateMipMaps )
				levels = MipMapSize(width, height);
#endif

#ifdef _XBOX
			{
				hr = CreateXboxTexture(width, height, levels, 0, destPixelFormat, &pMipMap);
			}
			if (FAILED(hr) || !pMipMap)
			{
				LogTextureMemoryPressure("CreateTexture2-rgba-failed",
					EstimateTextureBytes(destPixelFormat, width, height, levels), hr);
				XBLF("JA: fakegl CreateTexture retry registered tex=%d size=%dx%d levels=%d internal=0x%08x format=0x%08x dest=0x%08x hr=0x%08lx\n",
					m_textures.GetCurrentID(),
					width,
					height,
					levels,
					internalformat,
					format,
					destPixelFormat,
					(unsigned long)hr);
				hr = CreateRegisteredXboxTexture(width, height, levels, 0,
					destPixelFormat, &pMipMap, &registeredTextureBytes,
					&registeredTextureData);
				if (SUCCEEDED(hr) && pMipMap && registeredTextureData)
				{
					ownsTextureHeader = true;
					XBLF("JA: fakegl registered retry succeeded tex=%d ptr=%p bytes=%u\n",
						m_textures.GetCurrentID(), (void*)pMipMap, registeredTextureBytes);
				}
			}
#else
			hr = CreateXboxTexture(width, height, levels, 0, destPixelFormat, &pMipMap);
#endif

			if ( FAILED(hr) )
			{
#ifdef _XBOX
				TrackTextureFailure("rgba", (DWORD)width, (DWORD)height, (DWORD)levels,
					internalformat, format, destPixelFormat, hr,
					EstimateTextureBytes(destPixelFormat, width, height, levels));
				XBLF("JA: fakegl CreateTexture failed tex=%d size=%dx%d levels=%d internal=0x%08x format=0x%08x dest=0x%08x\n",
					m_textures.GetCurrentID(),
					width,
					height,
					levels,
					internalformat,
					format,
					destPixelFormat);
#endif
				if (!UseFallbackTexture(destPixelFormat, internalformat))
				{
					InterpretError(hr);
					return;
				}
#ifdef _XBOX
				XBLF("JA: fakegl using fallback texture tex=%d after allocation failure\n", m_textures.GetCurrentID());
#endif
				return;
			}
			m_textures.SetTexture(pMipMap, destPixelFormat, internalformat
#ifdef _XBOX
				, ownsTextureHeader
#endif
				);
#ifdef _XBOX
			if (!ownsTextureHeader)
			{
				TrackTextureAlloc("rgba", EstimateTextureBytes(destPixelFormat, width, height, levels));
			}
#endif
		}

#ifdef _XBOX
		if (m_textures.GetMipMap() == pMipMap && level == 0)
		{
			TextureEntry* currentEntry = m_textures.GetCurrentEntry();
			if (currentEntry && currentEntry->m_ownsTextureHeader && registeredTextureData)
			{
				if (UploadRegisteredLinearTexture(destPixelFormat, width, height,
						compatablePixels, compatablePixelsPitch,
						registeredTextureData, registeredTextureBytes))
				{
					m_textureState.DirtyTexture(m_textures.GetCurrentID());
					TrackTextureAlloc("rgba-registered", registeredTextureBytes ?
						registeredTextureBytes :
						EstimateTextureBytes(destPixelFormat, width, height, 1));
					return;
				}
				XBLF("JA: fakegl registered linear upload failed tex=%d image='%s'; falling back to LockRect path\n",
					m_textures.GetCurrentID(), FakeGL_CurrentTextureDebugName());
			}
		}
#endif

		glTexSubImage2D_Imp(pMipMap, level, 0, 0, width, height, format, type, compatablePixels, compatablePixelsPitch);

		if ( FAILED(hr) ) 
		{
			InterpretError(hr);
			return;
		}
	}

	bool UploadDDSTexture(GLint internalformat, GLsizei width, GLsizei height,
		GLint mipcount, const GLvoid *pixels, DWORD pixelBytes)
	{
		const char* debugName = FakeGL_CurrentTextureDebugName();
		const bool traceDds = FakeGL_ShouldTraceDDSImage(debugName);

		if (!m_pD3DDev || !pixels || width <= 0 || height <= 0)
		{
			if (traceDds)
			{
				XBLF("STEFX: DDS_TRACE fakegl reject image='%s' reason=bad-input tex=%d size=%dx%d internal=0x%08x bytes=%u",
					debugName, m_textures.GetCurrentID(), width, height,
					(unsigned int)internalformat, (unsigned int)pixelBytes);
			}
			return false;
		}

		D3DFORMAT destPixelFormat = D3DFMT_UNKNOWN;
		switch (internalformat)
		{
			case 0x9995: // GL_DDS1_EXT
				destPixelFormat = D3DFMT_DXT1;
				break;
			case 0x9996: // GL_DDS5_EXT
				destPixelFormat = D3DFMT_DXT5;
				break;
			case 0x9997: // GL_DDS_RGB16_EXT
				destPixelFormat = D3DFMT_R5G6B5;
				break;
			case 0x9998: // GL_DDS_RGBA32_EXT
				destPixelFormat = D3DFMT_A8R8G8B8;
				break;
			default:
				if (traceDds)
				{
					XBLF("STEFX: DDS_TRACE fakegl reject image='%s' reason=unknown-format tex=%d internal=0x%08x bytes=%u",
						debugName, m_textures.GetCurrentID(),
						(unsigned int)internalformat, (unsigned int)pixelBytes);
				}
				return false;
		}

		int levels = mipcount > 0 ? mipcount : 1;
		if (traceDds)
		{
			XBLF("STEFX: DDS_TRACE fakegl enter image='%s' tex=%d size=%dx%d levels=%d internal=0x%08x dest=0x%08x bytes=%u",
				debugName, m_textures.GetCurrentID(), width, height, levels,
				(unsigned int)internalformat, (unsigned int)destPixelFormat,
				(unsigned int)pixelBytes);
		}
#ifdef _XBOX
		int availableLevels = DDSAvailableLevels(destPixelFormat, (DWORD)width, (DWORD)height, pixelBytes);
		if (availableLevels <= 0)
		{
			if (traceDds)
			{
				XBLF("STEFX: DDS_TRACE fakegl reject image='%s' reason=no-complete-levels tex=%d size=%dx%d internal=0x%08x bytes=%u",
					debugName, m_textures.GetCurrentID(), width, height,
					(unsigned int)internalformat, (unsigned int)pixelBytes);
			}
			XBLF("JA: fakegl DDS upload rejected tex=%d size=%dx%d internal=0x%08x bytes=%u no complete levels\n",
				m_textures.GetCurrentID(), width, height, internalformat, pixelBytes);
			return false;
		}
		if (levels > availableLevels)
		{
			XBLF("JA: fakegl DDS mip clamp tex=%d size=%dx%d requested=%d available=%d bytes=%u\n",
				m_textures.GetCurrentID(), width, height, levels, availableLevels, pixelBytes);
			levels = availableLevels;
		}
		if (levels > 1)
		{
			static int s_ddsSingleMipLogs = 0;
			if (s_ddsSingleMipLogs < 64)
			{
				XBLF("JA: fakegl DDS single-level clamp tex=%d size=%dx%d oldLevels=%d bytes=%u\n",
					m_textures.GetCurrentID(), width, height, levels, pixelBytes);
				++s_ddsSingleMipLogs;
			}
			levels = 1;
		}
		const int xboxDdsMaxDim = 1024;
		const BYTE* ddsStart = (const BYTE*)pixels;
		DWORD ddsBytes = pixelBytes;
		int skippedTopMips = 0;
		while ((width > xboxDdsMaxDim || height > xboxDdsMaxDim) && levels > 1)
		{
			DWORD topBytes = DDSLevelRowBytes(destPixelFormat, (DWORD)width) * DDSLevelRows(destPixelFormat, (DWORD)height);
			if (!topBytes || ddsBytes <= topBytes)
				break;
			ddsStart += topBytes;
			ddsBytes -= topBytes;
			if (width > 1)
				width >>= 1;
			if (height > 1)
				height >>= 1;
			--levels;
			++skippedTopMips;
		}
		if (skippedTopMips)
		{
			XBLF("JA: fakegl DDS top-mip skip tex=%d skipped=%d newSize=%dx%d levels=%d bytes=%u\n",
				m_textures.GetCurrentID(), skippedTopMips, width, height, levels, ddsBytes);
		}
		if ((width > xboxDdsMaxDim || height > xboxDdsMaxDim) && levels == 1)
		{
			// Some DDS assets have only one stored level, so there is no lower mip to skip.
			// Use the first compressed blocks in a smaller texture to avoid late Cxbx allocation failure.
			GLsizei oldWidth = width;
			GLsizei oldHeight = height;
			while (width > xboxDdsMaxDim && width > 1)
				width >>= 1;
			while (height > xboxDdsMaxDim && height > 1)
				height >>= 1;
			XBLF("JA: fakegl DDS single-mip cap tex=%d oldSize=%dx%d newSize=%dx%d bytes=%u\n",
				m_textures.GetCurrentID(), oldWidth, oldHeight, width, height, ddsBytes);
		}
#endif
		IDirect3DTexture8* pMipMap = NULL;
		bool ownsTextureHeader = false;
		DWORD registeredTextureBytes = 0;
		void* registeredTextureData = NULL;
#ifdef _XBOX
		static int s_ddsDetailLogs = 0;
		const bool logDdsDetail = (s_ddsDetailLogs < 96);
		if (logDdsDetail)
		{
			XBLF("JA: fakegl DDS CreateTexture pre tex=%d size=%dx%d levels=%d dest=0x%08x bytes=%u registered=1",
				m_textures.GetCurrentID(), width, height, levels,
				(unsigned int)destPixelFormat, pixelBytes);
		}
		if (traceDds)
		{
			XBLF("STEFX: DDS_TRACE fakegl create-pre image='%s' tex=%d size=%dx%d levels=%d dest=0x%08x bytes=%u",
				debugName, m_textures.GetCurrentID(), width, height, levels,
				(unsigned int)destPixelFormat, (unsigned int)pixelBytes);
		}
		HRESULT hr = E_OUTOFMEMORY;
		if (destPixelFormat == D3DFMT_R5G6B5)
		{
			hr = CreateRegisteredXboxTexture(width, height, levels, 0, destPixelFormat, &pMipMap, &registeredTextureBytes, &registeredTextureData);
			if (SUCCEEDED(hr) && pMipMap && registeredTextureData)
			{
				ownsTextureHeader = true;
			}
			else
			{
				XBLF("JA: fakegl DDS RGB16 registered texture failed tex=%d hr=0x%08lx; retry CreateTexture2\n",
					m_textures.GetCurrentID(), (unsigned long)hr);
				registeredTextureData = NULL;
				registeredTextureBytes = 0;
				ownsTextureHeader = false;
			}
		}
		static int s_ddsCreateTexture2Logs = 0;
		if (!pMipMap && s_ddsCreateTexture2Logs < 32)
		{
			XBLF("JA: fakegl DDS registered texture deferred tex=%d; using CreateTexture2\n",
				m_textures.GetCurrentID());
			++s_ddsCreateTexture2Logs;
		}
		if (!pMipMap)
		{
			hr = CreateXboxTexture(width, height, levels, 0, destPixelFormat, &pMipMap);
		}
		if ((FAILED(hr) || !pMipMap) && !ownsTextureHeader)
		{
			HRESULT createTextureHr = hr;
			XBLF("JA: fakegl DDS CreateTexture2 failed tex=%d hr=0x%08lx; retry registered texture memory\n",
				m_textures.GetCurrentID(), (unsigned long)createTextureHr);
			hr = CreateRegisteredXboxTexture(width, height, levels, 0, destPixelFormat,
				&pMipMap, &registeredTextureBytes, &registeredTextureData);
			if (SUCCEEDED(hr) && pMipMap && registeredTextureData)
			{
				ownsTextureHeader = true;
				XBLF("JA: fakegl DDS registered retry succeeded tex=%d ptr=%p bytes=%u after hr=0x%08lx\n",
					m_textures.GetCurrentID(), (void*)pMipMap, registeredTextureBytes,
					(unsigned long)createTextureHr);
			}
			else
			{
				XBLF("JA: fakegl DDS registered retry failed tex=%d hr=0x%08lx createHr=0x%08lx\n",
					m_textures.GetCurrentID(), (unsigned long)hr,
					(unsigned long)createTextureHr);
				pMipMap = NULL;
				registeredTextureData = NULL;
				registeredTextureBytes = 0;
				ownsTextureHeader = false;
			}
		}
#else
		HRESULT hr = CreateXboxTexture(width, height, levels, 0, destPixelFormat, &pMipMap);
#endif
#ifdef _XBOX
		if (logDdsDetail)
		{
			XBLF("JA: fakegl DDS CreateTexture post tex=%d hr=0x%08lx ptr=%p registered=%d allocBytes=%u",
				m_textures.GetCurrentID(), (unsigned long)hr, (void*)pMipMap,
				ownsTextureHeader ? 1 : 0, registeredTextureBytes);
		}
		if (traceDds)
		{
			XBLF("STEFX: DDS_TRACE fakegl create-post image='%s' tex=%d hr=0x%08lx ptr=%p registered=%d allocBytes=%u",
				debugName, m_textures.GetCurrentID(), (unsigned long)hr,
				(void*)pMipMap, ownsTextureHeader ? 1 : 0,
				(unsigned int)registeredTextureBytes);
		}
#endif
		if (FAILED(hr) || !pMipMap)
		{
#ifdef _XBOX
			if (traceDds)
			{
				XBLF("STEFX: DDS_TRACE fakegl create-failed image='%s' tex=%d hr=0x%08lx fallback-attempt=1",
					debugName, m_textures.GetCurrentID(), (unsigned long)hr);
			}
			TrackTextureFailure("dds", (DWORD)width, (DWORD)height, (DWORD)levels,
				internalformat, internalformat, destPixelFormat, hr,
				EstimateTextureBytes(destPixelFormat, (DWORD)width, (DWORD)height, levels));
			LogTextureMemoryPressure("CreateTexture2-dds-failed",
				EstimateTextureBytes(destPixelFormat, (DWORD)width, (DWORD)height, levels), hr);
			XBLF("JA: fakegl DDS CreateTexture failed tex=%d size=%dx%d levels=%d internal=0x%08x dest=0x%08x bytes=%u\n",
				m_textures.GetCurrentID(), width, height, levels, internalformat,
				destPixelFormat, pixelBytes);
			if (UseFallbackTexture(destPixelFormat, internalformat))
			{
				if (traceDds)
				{
					XBLF("STEFX: DDS_TRACE fakegl fallback-used image='%s' tex=%d dest=0x%08x",
						debugName, m_textures.GetCurrentID(),
						(unsigned int)destPixelFormat);
				}
				XBLF("JA: fakegl DDS using fallback texture tex=%d after allocation failure\n",
					m_textures.GetCurrentID());
				return true;
			}
#endif
			return false;
		}

		const BYTE* src = (const BYTE*)pixels;
		DWORD remaining = pixelBytes;
#ifdef _XBOX
		src = ddsStart;
		remaining = ddsBytes;
		if (ownsTextureHeader && registeredTextureData && destPixelFormat == D3DFMT_R5G6B5)
		{
			const BYTE* sp = src;
			BYTE* dp = (BYTE*)registeredTextureData;
			DWORD levelWidth = (DWORD)width;
			DWORD levelHeight = (DWORD)height;
			DWORD copyBytes = 0;
			bool rgb16Ok = true;
			for (int level = 0; level < levels; ++level)
			{
				DWORD rowBytes = DDSLevelRowBytes(destPixelFormat, levelWidth);
				DWORD rows = DDSLevelRows(destPixelFormat, levelHeight);
				DWORD levelBytes = rowBytes * rows;
				if (remaining < levelBytes || copyBytes + levelBytes > registeredTextureBytes)
				{
					XBLF("JA: fakegl DDS RGB16 registered upload truncated tex=%d level=%d need=%u remaining=%u registered=%u copied=%u\n",
						m_textures.GetCurrentID(), level, levelBytes, remaining, registeredTextureBytes, copyBytes);
					rgb16Ok = false;
					break;
				}
				XGSwizzleRect(sp, rowBytes, NULL, dp, levelWidth, levelHeight, NULL, BytesPerPixel(destPixelFormat));
				sp += levelBytes;
				dp += levelBytes;
				remaining -= levelBytes;
				copyBytes += levelBytes;
				if (levelWidth > 1)
					levelWidth >>= 1;
				if (levelHeight > 1)
					levelHeight >>= 1;
			}
			if (rgb16Ok)
			{
				m_textures.SetTexture(pMipMap, destPixelFormat, internalformat, ownsTextureHeader);
				m_textureState.DirtyTexture(m_textures.GetCurrentID());
				if (logDdsDetail)
				{
					XBLF("JA: fakegl DDS RGB16 registered swizzle tex=%d bytes=%u registeredBytes=%u ptr=%p",
						m_textures.GetCurrentID(), copyBytes, registeredTextureBytes, registeredTextureData);
					++s_ddsDetailLogs;
				}
				TrackTextureAlloc("dds-rgb16-registered", copyBytes);
				return true;
			}
			delete (D3DTexture*)pMipMap;
			pMipMap = NULL;
			return false;
		}
		if (false && ownsTextureHeader && registeredTextureData &&
			(destPixelFormat == D3DFMT_DXT1 ||
			 destPixelFormat == D3DFMT_DXT3 ||
			 destPixelFormat == D3DFMT_DXT5))
		{
			DWORD copyBytes = remaining;
			if (registeredTextureBytes && copyBytes > registeredTextureBytes)
				copyBytes = registeredTextureBytes;
			memcpy(registeredTextureData, src, copyBytes);
			m_textures.SetTexture(pMipMap, destPixelFormat, internalformat, ownsTextureHeader);
			m_textureState.DirtyTexture(m_textures.GetCurrentID());
			if (logDdsDetail)
			{
				XBLF("JA: fakegl DDS registered direct copy tex=%d bytes=%u registeredBytes=%u ptr=%p",
					m_textures.GetCurrentID(), copyBytes, registeredTextureBytes, registeredTextureData);
				++s_ddsDetailLogs;
			}
			TrackTextureAlloc("dds", copyBytes);
			return true;
		}
#endif
		DWORD levelWidth = (DWORD)width;
		DWORD levelHeight = (DWORD)height;
		bool ok = true;

		for (int level = 0; level < levels; ++level)
		{
			D3DSURFACE_DESC desc;
#ifdef _XBOX
			if (logDdsDetail)
			{
				XBLF("JA: fakegl DDS GetLevelDesc pre tex=%d level=%d",
					m_textures.GetCurrentID(), level);
			}
#endif
			hr = pMipMap->GetLevelDesc(level, &desc);
#ifdef _XBOX
			if (logDdsDetail)
			{
				XBLF("JA: fakegl DDS GetLevelDesc post tex=%d level=%d hr=0x%08lx size=%ux%u fmt=0x%08x",
					m_textures.GetCurrentID(), level, (unsigned long)hr,
					(unsigned int)desc.Width, (unsigned int)desc.Height,
					(unsigned int)desc.Format);
			}
#endif
			if (FAILED(hr))
			{
				ok = false;
				break;
			}

			DWORD rowBytes = DDSLevelRowBytes(destPixelFormat, levelWidth);
			DWORD rows = DDSLevelRows(destPixelFormat, levelHeight);
			DWORD levelBytes = rowBytes * rows;
			if (remaining < levelBytes)
			{
#ifdef _XBOX
				XBLF("JA: fakegl DDS upload truncated tex=%d level=%d need=%u remaining=%u\n",
					m_textures.GetCurrentID(), level, levelBytes, remaining);
#endif
				ok = false;
				break;
			}

			D3DLOCKED_RECT lockedRect;
#ifdef _XBOX
			if (logDdsDetail)
			{
				XBLF("JA: fakegl DDS LockRect pre tex=%d level=%d rowBytes=%u rows=%u levelBytes=%u remaining=%u",
					m_textures.GetCurrentID(), level, rowBytes, rows, levelBytes, remaining);
			}
#endif
			hr = pMipMap->LockRect(level, &lockedRect, NULL, 0);
#ifdef _XBOX
			if (logDdsDetail)
			{
				XBLF("JA: fakegl DDS LockRect post tex=%d level=%d hr=0x%08lx bits=%p pitch=%ld",
					m_textures.GetCurrentID(), level, (unsigned long)hr,
					lockedRect.pBits, (long)lockedRect.Pitch);
			}
			if (traceDds)
			{
				XBLF("STEFX: DDS_TRACE fakegl lock image='%s' tex=%d level=%d hr=0x%08lx rowBytes=%u rows=%u pitch=%ld",
					debugName, m_textures.GetCurrentID(), level,
					(unsigned long)hr, (unsigned int)rowBytes,
					(unsigned int)rows, (long)lockedRect.Pitch);
			}
#endif
			if (FAILED(hr))
			{
#ifdef _XBOX
				XBLF("JA: fakegl DDS LockRect failed tex=%d level=%d size=%ux%u format=0x%08x\n",
					m_textures.GetCurrentID(), level, desc.Width, desc.Height, desc.Format);
#endif
				ok = false;
				break;
			}

			if (destPixelFormat == D3DFMT_DXT1 ||
				destPixelFormat == D3DFMT_DXT3 ||
				destPixelFormat == D3DFMT_DXT5)
			{
				const BYTE* sp = src;
				BYTE* dp = (BYTE*)lockedRect.pBits;
				for (DWORD y = 0; y < rows; ++y)
				{
					memcpy(dp, sp, rowBytes);
					sp += rowBytes;
					dp += lockedRect.Pitch;
				}
			}
			else
			{
				XGSwizzleRect(src, rowBytes, NULL, lockedRect.pBits,
					levelWidth, levelHeight, NULL, BytesPerPixel(destPixelFormat));
			}

#ifdef _XBOX
			if (logDdsDetail)
			{
				XBLF("JA: fakegl DDS UnlockRect pre tex=%d level=%d",
					m_textures.GetCurrentID(), level);
			}
#endif
			pMipMap->UnlockRect(level);
#ifdef _XBOX
			if (logDdsDetail)
			{
				XBLF("JA: fakegl DDS UnlockRect post tex=%d level=%d",
					m_textures.GetCurrentID(), level);
			}
#endif
			src += levelBytes;
			remaining -= levelBytes;
			if (levelWidth > 1)
				levelWidth >>= 1;
			if (levelHeight > 1)
				levelHeight >>= 1;
		}

		if (!ok)
		{
#ifdef _XBOX
			if (ownsTextureHeader)
			{
				delete (D3DTexture*)pMipMap;
			}
			else
#endif
			{
				pMipMap->Release();
			}
			return false;
		}

		m_textures.SetTexture(pMipMap, destPixelFormat, internalformat
#ifdef _XBOX
			, ownsTextureHeader
#endif
			);
		m_textureState.DirtyTexture(m_textures.GetCurrentID());
#ifdef _XBOX
		if (logDdsDetail)
		{
			XBLF("JA: fakegl DDS SetTextureEntry tex=%d ptr=%p",
				m_textures.GetCurrentID(), (void*)pMipMap);
			++s_ddsDetailLogs;
		}
		if (traceDds)
		{
			XBLF("STEFX: DDS_TRACE fakegl set-texture image='%s' tex=%d ptr=%p dest=0x%08x remaining=%u",
				debugName, m_textures.GetCurrentID(), (void*)pMipMap,
				(unsigned int)destPixelFormat, (unsigned int)remaining);
		}
		TrackTextureAlloc("dds", pixelBytes);
		static int s_ddsUploadLogs = 0;
		if (s_ddsUploadLogs < 96)
		{
			XBLF("JA: fakegl DDS upload tex=%d image='%s' size=%dx%d levels=%d internal=0x%08x dest=0x%08x bytes=%u\n",
				m_textures.GetCurrentID(), debugName, width, height, levels,
				internalformat, destPixelFormat, pixelBytes);
			++s_ddsUploadLogs;
		}
#endif
		return true;
	}

	void glTexParameterf (GLenum target, GLenum pname, GLfloat param)
	{
		switch(target)
		{
		case GL_TEXTURE_2D:
			{
				TextureEntry* current = m_textures.GetCurrentEntry();
				bool changed = false;
				
				switch(pname)
				{
				case GL_TEXTURE_MIN_FILTER:
					if (current->m_glTexParameter2DMinFilter != (GLint)param)
					{
						current->m_glTexParameter2DMinFilter = (GLint)param;
						changed = true;
					}
					break;
				case GL_TEXTURE_MAG_FILTER:
					if (current->m_glTexParameter2DMagFilter != (GLint)param)
					{
						current->m_glTexParameter2DMagFilter = (GLint)param;
						changed = true;
					}
					break;
				case GL_TEXTURE_WRAP_S:
					if (current->m_glTexParameter2DWrapS != (GLint)param)
					{
						current->m_glTexParameter2DWrapS = (GLint)param;
						changed = true;
					}
					break;
				case GL_TEXTURE_WRAP_T:
					if (current->m_glTexParameter2DWrapT != (GLint)param)
					{
						current->m_glTexParameter2DWrapT = (GLint)param;
						changed = true;
					}
					break;
				case D3D_TEXTURE_MAXANISOTROPY:
					if (current->m_maxAnisotropy != param)
					{
						current->m_maxAnisotropy = param;
						changed = true;
					}
					break;
				default:
					LocalDebugBreak();
				}

				if (changed)
				{
					SetRenderStateDirty();
					m_textureState.DirtyTexture(m_textures.GetCurrentID());
				}
			}
			break;
		default:
			LocalDebugBreak();
			break;
		}
	}

	void glTexSubImage2D (GLenum target, GLint level,
		GLint xoffset, GLint yoffset, GLsizei width, GLsizei height,
		GLenum format, GLenum type, const GLvoid *pixels)
	{
		if ( target != GL_TEXTURE_2D ) 
		{
			LocalDebugBreak();
			return;
		}

		if ( width <= 0 || height <= 0 )
			return;

		IDirect3DTexture8* pTexture = m_textures.GetMipMap();
		if ( ! pTexture ) 
			return;

		internalEnd(); // We may have a pending drawing using the old texture state.

		// To do: Convert the pixels on the fly while copying into the DX texture.

		char* compatablePixels = 0;
		DWORD compatablePixelsPitch;
		if ( FAILED(ConvertToCompatablePixels(m_textures.GetInternalFormat(),
				width, height,
				format, type, m_textures.GetSurfaceFormat(),
				pixels, &compatablePixels, &compatablePixelsPitch))) 
		{
			LocalDebugBreak();
			return;
		}

		glTexSubImage2D_Imp(pTexture, level, xoffset, yoffset, width, height, format, type, compatablePixels, compatablePixelsPitch);
	}

	char* StickyAlloc(DWORD size)
	{
		if ( m_stickyAllocSize < size ) 
		{
			delete [] m_stickyAlloc;
			m_stickyAlloc = new char[size];
			m_stickyAllocSize = size;
		}
		return m_stickyAlloc;
	}

	char* SubImageScratchAlloc(DWORD size)
	{
		if (m_subImageScratchSize < size)
		{
			delete [] m_subImageScratch;
			m_subImageScratch = new char[size];
			m_subImageScratchSize = size;
		}
		return m_subImageScratch;
	}

// Slower than just locking and unlocking. But both are really slow on NVIDIA hardware, due
// to texture swizzleing / unswizzleing.
// #define USE_IMAGE_SURFACE

	void glTexSubImage2D_Imp (IDirect3DTexture8* pMipMap, GLint level,
		GLint xoffset, GLint yoffset, GLsizei width, GLsizei height,
		GLenum /* format */, GLenum /* type */, const char* compatablePixels, int compatablePixelsPitch)
	{
		HRESULT hr = S_OK;
		D3DLOCKED_RECT lockedRect;
		D3DSURFACE_DESC desc;

		hr = pMipMap->GetLevelDesc(level, &desc);
		if ( FAILED(hr) )
		{
			InterpretError(hr);
			return;
		}

		if ( xoffset == 0 && yoffset == 0 && width == (GLsizei)desc.Width && height == (GLsizei)desc.Height )
		{
			D3DLOCKED_RECT swizzleRect;
			hr = pMipMap->LockRect(level, &swizzleRect, NULL, 0);
			if ( FAILED(hr) )
			{
#ifdef _XBOX
				XBLF("JA: fakegl direct swizzle LockRect failed tex=%d level=%d size=%dx%d format=0x%08x\n",
					m_textures.GetCurrentID(),
					level,
					desc.Width,
					desc.Height,
					desc.Format);
#endif
				InterpretError(hr);
				return;
			}

			XGSwizzleRect(
					compatablePixels,
					compatablePixelsPitch,
					NULL,
					swizzleRect.pBits,
					desc.Width,
					desc.Height,
					NULL,
					BytesPerPixel(desc.Format));
#ifdef _XBOX
			{
				static int s_efTexUploadLogBudget = 48;
				if (s_efTexUploadLogBudget > 0)
				{
					const unsigned char *srcBytes = (const unsigned char *)compatablePixels;
					XBLF("EF: TEXUPLOAD_SWIZZLE tex=%d level=%d size=%ux%u format=0x%08x bpp=%d pitch=%d src0=%02x,%02x,%02x,%02x",
						m_textures.GetCurrentID(),
						level,
						(unsigned int)desc.Width,
						(unsigned int)desc.Height,
						(unsigned int)desc.Format,
						BytesPerPixel(desc.Format),
						compatablePixelsPitch,
						srcBytes ? srcBytes[0] : 0,
						srcBytes ? srcBytes[1] : 0,
						srcBytes ? srcBytes[2] : 0,
						srcBytes ? srcBytes[3] : 0);
					--s_efTexUploadLogBudget;
				}
			}
#endif

			pMipMap->UnlockRect(level);
			return;
		}

		const int bytesPerPixel = BytesPerPixel(desc.Format);
		if (bytesPerPixel <= 0 ||
			xoffset < 0 || yoffset < 0 ||
			xoffset + width > (GLint)desc.Width ||
			yoffset + height > (GLint)desc.Height)
		{
#ifdef _XBOX
			XBLF("JA: fakegl CPU partial rejected tex=%d level=%d update=%dx%d at %d,%d surface=%dx%d format=0x%08x bpp=%d",
				m_textures.GetCurrentID(), level, width, height, xoffset, yoffset,
				desc.Width, desc.Height, desc.Format, bytesPerPixel);
#endif
			return;
		}

		const DWORD linearPitch = desc.Width * bytesPerPixel;
		const DWORD linearBytes = linearPitch * desc.Height;
		char* linearPixels = SubImageScratchAlloc(linearBytes);
		if (!linearPixels)
		{
#ifdef _XBOX
			XBLF("JA: fakegl CPU partial scratch alloc failed tex=%d bytes=%u", m_textures.GetCurrentID(), linearBytes);
#endif
			return;
		}

		hr = pMipMap->LockRect(level, &lockedRect, NULL, 0);
		if (FAILED(hr))
		{
#ifdef _XBOX
			XBLF("JA: fakegl CPU partial LockRect failed tex=%d level=%d hr=0x%08lx", m_textures.GetCurrentID(), level, (unsigned long)hr);
#endif
			InterpretError(hr);
			return;
		}

		XGUnswizzleRect(lockedRect.pBits, desc.Width, desc.Height, NULL,
			linearPixels, linearPitch, NULL, bytesPerPixel);

		const DWORD copyBytes = width * bytesPerPixel;
		if ((DWORD)compatablePixelsPitch < copyBytes)
		{
#ifdef _XBOX
			XBLF("JA: fakegl CPU partial source pitch rejected tex=%d pitch=%d copy=%u", m_textures.GetCurrentID(), compatablePixelsPitch, copyBytes);
#endif
			pMipMap->UnlockRect(level);
			return;
		}

		{
			const char* sp = compatablePixels;
			char* dp = linearPixels + (yoffset * linearPitch) + (xoffset * bytesPerPixel);
			for (int y = 0; y < height; ++y)
			{
				memcpy(dp, sp, copyBytes);
				sp += compatablePixelsPitch;
				dp += linearPitch;
			}
		}

		XGSwizzleRect(linearPixels, linearPitch, NULL, lockedRect.pBits,
			desc.Width, desc.Height, NULL, bytesPerPixel);
		pMipMap->UnlockRect(level);
#ifdef _XBOX
		{
			static int s_cpuPartialLogs = 0;
			if (s_cpuPartialLogs < 24)
			{
				XBLF("JA: fakegl CPU partial tex=%d level=%d update=%dx%d at %d,%d surface=%dx%d format=0x%08x bpp=%d scratch=%u",
					m_textures.GetCurrentID(), level, width, height, xoffset, yoffset,
					desc.Width, desc.Height, desc.Format, bytesPerPixel, linearBytes);
				++s_cpuPartialLogs;
			}
		}
#endif
	}

	inline void glTranslatef (GLfloat x, GLfloat y, GLfloat z)
	{
		SetRenderStateDirty();
		D3DXMATRIX m;
		D3DXMatrixTranslation(&m, x, y, z);
		m_currentMatrixStack->MultMatrixLocal(&m);
		*m_currentMatrixStateDirty = true;
	}

	inline void glVertex2f (GLfloat x, GLfloat y)
	{
		m_OGLPrimitiveVertexBuffer.SetVertex(x, y, 0);
	}

	inline void glVertex3f (GLfloat x, GLfloat y, GLfloat z)
	{
		m_OGLPrimitiveVertexBuffer.SetVertex(x, y, z);
	}

	inline void glVertex3fv (const GLfloat *v)
	{
		m_OGLPrimitiveVertexBuffer.SetVertex(v[0], v[1], v[2]);
	}

	void glViewport (GLint x, GLint y, GLsizei width, GLsizei height)
	{
		if ( m_glViewPortX != x || m_glViewPortY != y ||
			m_glViewPortWidth != width || m_glViewPortHeight != height ) 
		{
			SetRenderStateDirty();
			m_glViewPortX = x;
			m_glViewPortY = y;
			m_glViewPortWidth = width;
			m_glViewPortHeight = height;

			m_bViewPortDirty = true;
		}
	}

#ifdef _XBOX
	void UpdateFramebufferTelemetry(bool afterPresent)
	{
		static int s_framebufferProbeLogCount = 0;
		static int s_framebufferPreSampleLogCount = 0;
		static int s_framebufferPostSampleLogCount = 0;
		if (!m_pD3DDev)
		{
			if (s_framebufferProbeLogCount < 16)
			{
				XBLF("STEFX: fakegl framebuffer probe #%d skipped no device", s_framebufferProbeLogCount);
				++s_framebufferProbeLogCount;
			}
			return;
		}

		D3DSurface *backBuffer = NULL;
		const bool screenshotRequested = FakeGL_ScreenshotRequested();
		int *sampleLogCount = afterPresent ? &s_framebufferPostSampleLogCount : &s_framebufferPreSampleLogCount;
		const int sampleLogLimit = afterPresent ? 16 : 32;
		if (!screenshotRequested && *sampleLogCount >= sampleLogLimit)
		{
			return;
		}
		const int backBufferIndex = afterPresent ? -1 : 0;
		if (screenshotRequested)
		{
			D3DDevice_BlockUntilIdle();
		}
		HRESULT hrBackBuffer = m_pD3DDev->GetBackBuffer(backBufferIndex, D3DBACKBUFFER_TYPE_MONO, &backBuffer);
		if (FAILED(hrBackBuffer) || !backBuffer)
		{
			if (s_framebufferProbeLogCount < 16)
			{
				XBLF("STEFX: fakegl framebuffer probe #%d GetBackBuffer request=%d afterPresent=%d index=%d hr=0x%08lx surf=%08x",
					s_framebufferProbeLogCount,
					screenshotRequested ? 1 : 0,
					afterPresent ? 1 : 0,
					backBufferIndex,
					(unsigned long)hrBackBuffer,
					(unsigned int)backBuffer);
				++s_framebufferProbeLogCount;
			}
			return;
		}

		const DWORD packedSize = backBuffer->Size;
		DWORD width = m_d3dsdBackBuffer.Width ? m_d3dsdBackBuffer.Width : gWidth;
		DWORD height = m_d3dsdBackBuffer.Height ? m_d3dsdBackBuffer.Height : gHeight;
		DWORD pitch = width * 4;
		if (packedSize)
		{
			width = (packedSize & D3DSIZE_WIDTH_MASK) + 1;
			height = ((packedSize & D3DSIZE_HEIGHT_MASK) >> D3DSIZE_HEIGHT_SHIFT) + 1;
			pitch = (((packedSize & D3DSIZE_PITCH_MASK) >> D3DSIZE_PITCH_SHIFT) + 1) * 64;
		}

		g_SPXBFramebufferData = backBuffer->Data;
		g_SPXBFramebufferPitch = pitch;
		g_SPXBFramebufferWidth = width;
		g_SPXBFramebufferHeight = height;
		g_SPXBFramebufferFormat = backBuffer->Format;
		g_SPXBFramebufferSize = packedSize;

		FakeGL_EnsureScreenshotFilePreopened();
		FakeGL_TryWriteRequestedBackbufferBMP(m_pD3DDev, backBuffer, width, height, pitch, afterPresent ? "post-present" : "pre-present");

		if (*sampleLogCount < sampleLogLimit)
		{
			DWORD nonzero = 0;
			DWORD checksum = 0;
			DWORD firstPixel = 0;
			DWORD centerPixel = 0;
			DWORD cornerPixel = 0;

			__try
			{
				const BYTE *data = (const BYTE *)backBuffer->Data;
				if (data && pitch >= width * 4 && width > 0 && height > 0)
				{
					for (DWORD sy = 0; sy < 4; ++sy)
					{
						DWORD y = (height - 1) * sy / 3;
						for (DWORD sx = 0; sx < 4; ++sx)
						{
							DWORD x = (width - 1) * sx / 3;
							DWORD pixel = *(const DWORD *)(data + y * pitch + x * 4);
							if (pixel & 0x00ffffff)
							{
								nonzero++;
							}
							checksum = (checksum * 1664525u) + pixel + 1013904223u;
						}
					}
					firstPixel = *(const DWORD *)data;
					centerPixel = *(const DWORD *)(data + (height / 2) * pitch + (width / 2) * 4);
					cornerPixel = *(const DWORD *)(data + (height - 1) * pitch + (width - 1) * 4);
				}
			}
			__except(EXCEPTION_EXECUTE_HANDLER)
			{
				checksum = 0xffffffffu;
			}

			XBLF("STEFX: fakegl framebuffer sample #%d afterPresent=%d index=%d data=0x%08x pitch=%u size=%ux%u fmt=0x%08x nonzero=%u checksum=0x%08x first=0x%08x center=0x%08x corner=0x%08x\n",
				*sampleLogCount,
				afterPresent ? 1 : 0,
				backBufferIndex,
				(unsigned int)backBuffer->Data,
				(unsigned int)pitch,
				(unsigned int)width,
				(unsigned int)height,
				(unsigned int)backBuffer->Format,
				(unsigned int)nonzero,
				(unsigned int)checksum,
				(unsigned int)firstPixel,
				(unsigned int)centerPixel,
				(unsigned int)cornerPixel);
			++(*sampleLogCount);
			++s_framebufferProbeLogCount;
		}
	}
#endif

	void SwapBuffers()
	{
#ifdef _XBOX
		static int s_xboxSwapLogCount = 0;
		g_stefxFakeglSwapFrame = s_xboxSwapLogCount;
		const bool stefxLateSwap = false;
		const bool stefxSwapProbe = s_xboxSwapLogCount < 16;
		const bool logSwapTight = stefxLateSwap;
		const bool logSwap = (logSwapTight || s_xboxSwapLogCount < 8 || ((s_xboxSwapLogCount & 255) == 0));
		if (stefxSwapProbe)
		{
			XBLF("STEFX: fakegl swap #%d enter dev=%p needBeginScene=%d",
				s_xboxSwapLogCount, (void*)m_pD3DDev, (int)m_needBeginScene);
		}
		if (stefxLateSwap)
		{
			XBLF("STEFX: LATE fakegl SwapBuffers #%d enter dev=%p needBeginScene=%d",
				s_xboxSwapLogCount, (void*)m_pD3DDev, (int)m_needBeginScene);
		}
		if (logSwap)
		{
			XBLF("STEFX: fakegl SwapBuffers #%d enter dev=%p needBeginScene=%d",
				s_xboxSwapLogCount, (void*)m_pD3DDev, (int)m_needBeginScene);
		}
		if (logSwapTight)
		{
			XBLF("STEFX: CL_EARLY fakegl SwapBuffers #%d enter dev=%p needBeginScene=%d",
				s_xboxSwapLogCount, (void*)m_pD3DDev, (int)m_needBeginScene);
		}
#endif
	#ifdef _XBOX
		if (logSwapTight) XBLF("STEFX: CL_EARLY fakegl SwapBuffers #%d before internalEnd", s_xboxSwapLogCount);
		if (stefxLateSwap) XBLF("STEFX: LATE fakegl SwapBuffers #%d before internalEnd", s_xboxSwapLogCount);
		if (stefxSwapProbe) XBLF("STEFX: fakegl swap #%d before internalEnd", s_xboxSwapLogCount);
	#endif
		internalEnd();
	#ifdef _XBOX
		if (logSwapTight) XBLF("STEFX: CL_EARLY fakegl SwapBuffers #%d after internalEnd", s_xboxSwapLogCount);
		if (stefxLateSwap) XBLF("STEFX: LATE fakegl SwapBuffers #%d after internalEnd", s_xboxSwapLogCount);
		if (stefxSwapProbe) XBLF("STEFX: fakegl swap #%d after internalEnd", s_xboxSwapLogCount);
	#endif
		if (!m_pD3DDev)
		{
#ifdef _XBOX
			XBLog_Write("STEFX: fakegl SwapBuffers skipped because m_pD3DDev is NULL");
			s_xboxSwapLogCount++;
#endif
			return;
		}
#ifdef _XBOX
		if (stefxLateSwap) XBLF("STEFX: LATE fakegl SwapBuffers #%d before render probe", s_xboxSwapLogCount);
		FakeGL_DrawRenderProbe(m_pD3DDev);
		DrawTextureProbe();
		if (stefxLateSwap) XBLF("STEFX: LATE fakegl SwapBuffers #%d after render probe", s_xboxSwapLogCount);
#endif
	#ifdef _XBOX
		if (logSwapTight) XBLF("STEFX: CL_EARLY fakegl SwapBuffers #%d before EndScene", s_xboxSwapLogCount);
		if (stefxLateSwap) XBLF("STEFX: LATE fakegl SwapBuffers #%d before EndScene", s_xboxSwapLogCount);
		if (stefxSwapProbe) XBLF("STEFX: fakegl swap #%d before EndScene", s_xboxSwapLogCount);
	#endif
		HRESULT hrEndScene = m_pD3DDev->EndScene();
#ifdef _XBOX
		if (stefxLateSwap) XBLF("STEFX: LATE fakegl SwapBuffers #%d EndScene hr=0x%08lx", s_xboxSwapLogCount, (unsigned long)hrEndScene);
		if (stefxSwapProbe) XBLF("STEFX: fakegl swap #%d EndScene hr=0x%08lx", s_xboxSwapLogCount, (unsigned long)hrEndScene);
		if (logSwapTight)
		{
			XBLF("STEFX: CL_EARLY fakegl SwapBuffers #%d EndScene hr=0x%08lx", s_xboxSwapLogCount, (unsigned long)hrEndScene);
		}
		if (logSwap)
		{
			XBLF("STEFX: fakegl SwapBuffers #%d EndScene hr=0x%08lx", s_xboxSwapLogCount, (unsigned long)hrEndScene);
		}
#endif
		m_needBeginScene = true;
	#ifdef _XBOX
		if (stefxLateSwap) XBLF("STEFX: LATE fakegl SwapBuffers #%d before KickPushBuffer", s_xboxSwapLogCount);
	#endif
#if 0
		static int frameCounter;
		frameCounter++;
		char buf[100];
		sprintf(buf, "Present %d\n", frameCounter);
		OutputDebugString(buf);
#endif


#if 0 //PROFILE
#define MB	(1024*1024)
#define AddStr(a,b) (pstrOut += wsprintf( pstrOut, a, b ))

		MEMORYSTATUS stat;
		CHAR strOut[1024], *pstrOut;

		// Get the memory status.
		GlobalMemoryStatus( &stat );

		// Setup the output string.
		pstrOut = strOut;
		AddStr( "%4d total MB of virtual memory.\n", stat.dwTotalVirtual / MB );
		AddStr( "%4d  free MB of virtual memory.\n", stat.dwAvailVirtual / MB );
		AddStr( "%4d total MB of physical memory.\n", stat.dwTotalPhys / MB );
		AddStr( "%4d  free MB of physical memory.\n", stat.dwAvailPhys / MB );
		AddStr( "%4d total MB of paging file.\n", stat.dwTotalPageFile / MB );
		AddStr( "%4d  free MB of paging file.\n", stat.dwAvailPageFile / MB );
		AddStr( "%4d  percent of memory is in use.\n", stat.dwMemoryLoad );

		// Output the string.
		OutputDebugString( strOut );
#endif

#ifdef PROFILE
D3DPERF_SetShowFrameRateInterval( 1000 );
#endif

#ifdef _XBOX
		if (logSwap)
		{
			XBLF("STEFX: fakegl SwapBuffers #%d Present...", s_xboxSwapLogCount);
		}
		if (logSwapTight) XBLF("STEFX: CL_EARLY fakegl SwapBuffers #%d before Present", s_xboxSwapLogCount);
		if (stefxLateSwap) XBLF("STEFX: LATE fakegl SwapBuffers #%d before Present", s_xboxSwapLogCount);
		const bool screenshotRequestedNow = FakeGL_ScreenshotRequested();
#if defined(STEFX_ELITE_FORCE_MP)
		const bool cxbxPresentThrottle = false;
#else
		const bool cxbxPresentThrottle = FakeGL_FileExistsOnAnyRuntimeDrive(STEFX_CXBX_PRESENT_THROTTLE_FILE);
#endif
		const bool skipPresentForCxbx = cxbxPresentThrottle &&
			s_xboxSwapLogCount >= 96;
		static int s_cxbxPresentThrottleLogBudget = 4;
		if (s_xboxSwapLogCount < 64)
		{
			UpdateFramebufferTelemetry(false);
		}
		if (screenshotRequestedNow)
		{
			XBLF("STEFX: renderer pre-present capture requested swap=%d screenshot=%d probe=%d",
				s_xboxSwapLogCount, screenshotRequestedNow ? 1 : 0, stefxSwapProbe ? 1 : 0);
			UpdateFramebufferTelemetry(false);
		}
		if (skipPresentForCxbx && s_cxbxPresentThrottleLogBudget > 0)
		{
			XBLF("STEFX: CXBX present throttle skipping Present swap=%d screenshot=%d",
				s_xboxSwapLogCount, screenshotRequestedNow ? 1 : 0);
			--s_cxbxPresentThrottleLogBudget;
		}
#endif
		HRESULT hrPresent;
#ifdef _XBOX
		hrPresent = skipPresentForCxbx ? S_OK : m_pD3DDev->Present(NULL, NULL, NULL, NULL);
#else
        hrPresent = m_pD3DDev->Present(NULL, NULL, NULL, NULL);
#endif
#ifdef _XBOX
		if (stefxLateSwap) XBLF("STEFX: LATE fakegl SwapBuffers #%d Present hr=0x%08lx", s_xboxSwapLogCount, (unsigned long)hrPresent);
		if (stefxSwapProbe) XBLF("STEFX: fakegl swap #%d Present hr=0x%08lx", s_xboxSwapLogCount, (unsigned long)hrPresent);
		if (!skipPresentForCxbx && (screenshotRequestedNow || s_xboxSwapLogCount < 16))
		{
			UpdateFramebufferTelemetry(true);
		}
		if (logSwapTight)
		{
			XBLF("STEFX: CL_EARLY fakegl SwapBuffers #%d Present hr=0x%08lx", s_xboxSwapLogCount, (unsigned long)hrPresent);
		}
		if (logSwap)
		{
			XBLF("STEFX: fakegl SwapBuffers #%d Present hr=0x%08lx", s_xboxSwapLogCount, (unsigned long)hrPresent);
		}
		s_xboxSwapLogCount++;
#endif
#if 0
		if ( frameCounter == 3 )
		{
			Sleep(1700);
			LocalDebugBreak();
		}
#endif
	}

	void SetGammaRamp(const unsigned char* gammaTable)
	{
		D3DGAMMARAMP gammaRamp;
		for(int i = 0; i < 256; i++ ) 
		{
			WORD value = gammaTable[i];
			value = value + (value << 8); // * 257
			gammaRamp.red[i] = value;
			gammaRamp.green[i] = value;
			gammaRamp.blue[i] = value;
		}
		m_pD3DDev->SetGammaRamp(D3DSGR_NO_CALIBRATION, &gammaRamp);
	}

	void Hint_GenerateMipMaps(int value)
	{
		m_hintGenerateMipMaps = value != 0;
	}

	void EvictTextures()
	{
		// MARTY - Not available on the XBox!
		//m_pD3DDev->ResourceManagerDiscardBytes(0);
	}
#ifdef _XBOX
	void DrawTextureProbe()
	{
		if (!m_pD3DDev || !FakeGL_RenderProbeRequested())
		{
			return;
		}

		static int s_probeLogCount = 0;
		const GLuint probeIds[] = { 47, 49, 60, 61, 65, 82, 100 };
		TextureEntry *entry = NULL;
		GLuint texId = 0;
		for (int i = 0; i < sizeof(probeIds) / sizeof(probeIds[0]); ++i)
		{
			TextureEntry *candidate = m_textures.GetEntry(probeIds[i]);
			if (candidate && candidate->m_mipMap)
			{
				entry = candidate;
				texId = probeIds[i];
				break;
			}
		}
		if (!entry)
		{
			if (s_probeLogCount < 16)
			{
				XBLF("STEFX: fakegl texture probe skipped; no candidate texture loaded");
				++s_probeLogCount;
			}
			return;
		}

		struct probeVertex_t
		{
			float x;
			float y;
			float z;
			float rhw;
			DWORD color;
			float s;
			float t;
		};

		const probeVertex_t verts[4] =
		{
			{  32.0f,  32.0f, 0.0f, 1.0f, 0xffffffff, 0.0f, 0.0f },
			{ 288.0f,  32.0f, 0.0f, 1.0f, 0xffffffff, 1.0f, 0.0f },
			{  32.0f, 288.0f, 0.0f, 1.0f, 0xffffffff, 0.0f, 1.0f },
			{ 288.0f, 288.0f, 0.0f, 1.0f, 0xffffffff, 1.0f, 1.0f },
		};

		m_pD3DDev->SetTexture(0, entry->m_mipMap);
		m_pD3DDev->SetTexture(1, NULL);
		m_pD3DDev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		m_pD3DDev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
		m_pD3DDev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
		m_pD3DDev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		m_pD3DDev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
		m_pD3DDev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
		m_pD3DDev->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
		m_pD3DDev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
		m_pD3DDev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
		m_pD3DDev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		m_pD3DDev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
		m_pD3DDev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
		m_pD3DDev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
		m_pD3DDev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
		m_pD3DDev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
		HRESULT hrShader = m_pD3DDev->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
		HRESULT hrDraw = m_pD3DDev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, verts, sizeof(verts[0]));
		m_pD3DDev->KickPushBuffer();
		m_pD3DDev->BlockUntilIdle();

		if (s_probeLogCount < 64)
		{
			XBLF("STEFX: fakegl texture probe tex=%u ptr=%p fmt=0x%08x internal=0x%08x shader=0x%08lx draw=0x%08lx",
				(unsigned int)texId,
				(void*)entry->m_mipMap,
				(unsigned int)entry->m_format,
				(unsigned int)entry->m_internalFormat,
				(unsigned long)hrShader,
				(unsigned long)hrDraw);
			++s_probeLogCount;
		}
	}
#endif
private:

	void SetRenderStateDirty()
	{
		if ( ! m_glRenderStateDirty )
		{
			internalEnd();
			m_glRenderStateDirty = true;
		}
	}

	HRESULT HandleWindowedModeChanges()
	{
		return S_OK;
	}

	void SetGLRenderState()
	{
		if ( ! m_glRenderStateDirty )
		{
			return;
		}
		m_glRenderStateDirty = false;
		HRESULT hr;
		if ( m_glAlphaStateDirty )
		{
			m_glAlphaStateDirty = false;
			// Alpha test
			const DWORD alphaFunc = m_glAlphaTest ? GLToDXCompare(m_glAlphaFunc) : D3DCMP_ALWAYS;
			const DWORD alphaRef = (DWORD)(255 * m_glAlphaFuncRef);
			m_pD3DDev->SetRenderState( D3DRS_ALPHATESTENABLE,
				m_glAlphaTest ? TRUE : FALSE );
			m_pD3DDev->SetRenderState(D3DRS_ALPHAFUNC, alphaFunc);
			m_pD3DDev->SetRenderState(D3DRS_ALPHAREF, alphaRef);
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
			if ( m_glAlphaTest )
			{
				static int s_stefxAlphaStateLogBudget = 64;
				if ( s_stefxAlphaStateLogBudget > 0 )
				{
					XBLF("STEFX_ALPHA_STATE enable=1 glFunc=0x%08x dxFunc=%lu ref=%lu blend=%d depthFunc=0x%08x depthMask=%d",
						(unsigned int)m_glAlphaFunc,
						(unsigned long)alphaFunc,
						(unsigned long)alphaRef,
						m_glBlend ? 1 : 0,
						(unsigned int)m_glDepthFunc,
						m_glDepthMask ? 1 : 0);
					--s_stefxAlphaStateLogBudget;
				}
			}
#endif
		}
		if ( m_glBlendStateDirty )
		{
			m_glBlendStateDirty = false;
			// Alpha blending
			DWORD srcBlend = m_glBlend ? GLToDXSBlend(m_glBlendFuncSFactor) : D3DBLEND_ONE;
			DWORD destBlend = m_glBlend ? GLToDXDBlend(m_glBlendFuncDFactor) : D3DBLEND_ZERO;
			m_pD3DDev->SetRenderState( D3DRS_SRCBLEND,  srcBlend );
			m_pD3DDev->SetRenderState( D3DRS_DESTBLEND, destBlend );
			m_pD3DDev->SetRenderState( D3DRS_ALPHABLENDENABLE, m_glBlend ? TRUE : FALSE );
		}
		if ( m_glCullStateDirty ) 
		{
			m_glCullStateDirty = false;
			D3DCULL cull = D3DCULL_NONE;
			if ( m_glCullFace ) 
			{
				switch(m_glCullFaceMode)
				{
				default:
				case GL_BACK:
					cull = D3DCULL_CW;
					break;
				case GL_FRONT:
					cull = D3DCULL_CCW;
					break;
				}
			}
			hr = m_pD3DDev->SetRenderState(D3DRS_CULLMODE, cull);
			if ( FAILED(hr) ){
				InterpretError(hr);
			}
		}
#ifdef _XBOX
		{
			D3DFILLMODE frontFill = (m_glPolygonModeFront == GL_LINE) ? D3DFILL_WIREFRAME : D3DFILL_SOLID;
			D3DFILLMODE backFill = (m_glPolygonModeBack == GL_LINE) ? D3DFILL_WIREFRAME : D3DFILL_SOLID;
			m_pD3DDev->SetRenderState(D3DRS_FILLMODE, frontFill);
			m_pD3DDev->SetRenderState(D3DRS_BACKFILLMODE, backFill);
			if (frontFill != D3DFILL_SOLID || backFill != D3DFILL_SOLID)
			{
				static int s_fillModeLogCount = 0;
				if (s_fillModeLogCount < 32)
				{
					XBLF("JA: fakegl polygon fill front=0x%04x back=0x%04x dxFront=0x%08x dxBack=0x%08x",
						(unsigned int)m_glPolygonModeFront,
						(unsigned int)m_glPolygonModeBack,
						(unsigned int)frontFill,
						(unsigned int)backFill);
					++s_fillModeLogCount;
				}
			}
		}
#endif
		if ( m_glShadeModelStateDirty )
		{
			m_glShadeModelStateDirty = false;
			// Shade model
			m_pD3DDev->SetRenderState( D3DRS_SHADEMODE, 
				m_glShadeModel == GL_SMOOTH ? D3DSHADE_GOURAUD : D3DSHADE_FLAT );
		}
			
		{
			m_textureState.SetTextureStageState(m_pD3DDev, &m_textures);
		}

		if ( m_glDepthStateDirty ) 
		{
			m_glDepthStateDirty = false;
			m_pD3DDev->SetRenderState( D3DRS_ZENABLE, m_glDepthTest ? D3DZB_TRUE : D3DZB_FALSE);
			m_pD3DDev->SetRenderState( D3DRS_ZWRITEENABLE, m_glDepthMask ? TRUE : FALSE);
			DWORD zfunc = GLToDXCompare(m_glDepthFunc);
			m_pD3DDev->SetRenderState( D3DRS_ZFUNC, zfunc );
		}
		if ( m_glFogStateDirty )
		{
			m_glFogStateDirty = false;
			m_pD3DDev->SetRenderState( D3DRS_FOGENABLE, m_glFog ? TRUE : FALSE );
			m_pD3DDev->SetRenderState( D3DRS_FOGTABLEMODE, GLToDXFogMode(m_glFogMode) );
			m_pD3DDev->SetRenderState( D3DRS_FOGDENSITY, *(DWORD*)&m_glFogDensity );
			m_pD3DDev->SetRenderState( D3DRS_FOGSTART, *(DWORD*)&m_glFogStart );
			m_pD3DDev->SetRenderState( D3DRS_FOGEND, *(DWORD*)&m_glFogEnd );
			m_pD3DDev->SetRenderState( D3DRS_FOGCOLOR, m_glFogColor );
#ifdef _XBOX
			{
				static int s_fogApplyLogCount = 0;
				if (s_fogApplyLogCount < 24)
				{
					XBLF("JA: fakegl fog apply enabled=%d mode=0x%04x start=%g end=%g density=%g color=0x%08x",
						m_glFog ? 1 : 0,
						(unsigned int)m_glFogMode,
						(float)m_glFogStart,
						(float)m_glFogEnd,
						(float)m_glFogDensity,
						(unsigned int)m_glFogColor);
				}
				++s_fogApplyLogCount;
			}
#endif
		}
		if ( m_modelViewMatrixStateDirty ) 
		{
			m_modelViewMatrixStateDirty = false;
			m_pD3DDev->SetTransform( D3DTS_WORLD, m_modelViewMatrixStack->GetTop() );
		}
		if ( m_viewMatrixStateDirty ) 
		{
			m_viewMatrixStateDirty = false;
			m_pD3DDev->SetTransform( D3DTS_VIEW, & m_d3dViewMatrix );
		}
		if ( m_projectionMatrixStateDirty ) 
		{
			m_projectionMatrixStateDirty = false;
			m_pD3DDev->SetTransform( D3DTS_PROJECTION, m_projectionMatrixStack->GetTop() );
		}
		if ( m_glClipPlane0StateDirty )
		{
			m_glClipPlane0StateDirty = false;
#ifdef _XBOX
			static int s_clipPlaneApplyLogCount = 0;
			if (s_clipPlaneApplyLogCount < 16)
			{
				XBLF("JA: fakegl GL_CLIP_PLANE0 apply skipped on Xbox fixed-function path enabled=%d plane=%g,%g,%g,%g",
					m_glClipPlane0Enabled ? 1 : 0,
					m_glClipPlane0[0],
					m_glClipPlane0[1],
					m_glClipPlane0[2],
					m_glClipPlane0[3]);
			}
			++s_clipPlaneApplyLogCount;
#else
			m_pD3DDev->SetClipPlane( 0, m_glClipPlane0 );
			m_pD3DDev->SetRenderState( D3DRS_CLIPPLANEENABLE,
				m_glClipPlane0Enabled ? 1 : 0 );
#endif
		}
		if ( m_textureMatrixStateDirty )
		{
			m_textureMatrixStateDirty = false;
			m_pD3DDev->SetTransform( D3DTS_TEXTURE0, m_textureMatrixStack->GetTop() );
		}
		if ( m_bViewPortDirty )
		{
			m_bViewPortDirty = false;
			D3DVIEWPORT8 viewData;

			GLint viewportX = m_glViewPortX;
			GLint viewportY = m_windowHeight - (m_glViewPortY + m_glViewPortHeight);
			if ( viewportX < 0 )
			{
				viewportX = 0;
			}
			if ( viewportY < 0 )
			{
				viewportY = 0;
			}

			viewData.X = viewportX;
			viewData.Y = viewportY;
			viewData.Width  = m_glViewPortWidth;
			viewData.Height = m_glViewPortHeight;
			viewData.MinZ = m_glDepthRangeNear;     
			viewData.MaxZ = m_glDepthRangeFar;
#ifdef _XBOX
			{
				static int s_xboxViewportApplyLogBudget = 16;
				if (s_xboxViewportApplyLogBudget > 0)
				{
					XBLF("JA: fakegl SetViewport requested=%d,%d %dx%d applied=%lu,%lu %lux%lu z=%g..%g",
						m_glViewPortX,
						m_glViewPortY,
						m_glViewPortWidth,
						m_glViewPortHeight,
						(unsigned long)viewData.X,
						(unsigned long)viewData.Y,
						(unsigned long)viewData.Width,
						(unsigned long)viewData.Height,
						(float)viewData.MinZ,
						(float)viewData.MaxZ);
					--s_xboxViewportApplyLogBudget;
				}
			}
#endif
			m_pD3DDev->SetViewport(&viewData);
		}
	}

	void EnsureDriverInfo() 
	{
		if ( ! m_vendor ) 
		{
			if ( m_pD3D )
			{
				m_pD3D->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &m_dddi);
				m_vendor = m_dddi.Driver;
				m_renderer = m_dddi.Description;
				wsprintf(m_version, "%u.%u.%u.%u %u.%u.%u.%u %u", 
					HIWORD(m_dddi.DriverVersion.HighPart),
					LOWORD(m_dddi.DriverVersion.HighPart),
					HIWORD(m_dddi.DriverVersion.LowPart),
					LOWORD(m_dddi.DriverVersion.LowPart),
					m_dddi.VendorId,
					m_dddi.DeviceId,
					m_dddi.SubSysId,
					m_dddi.Revision,
					m_dddi.WHQLLevel
					);
			}
			else
			{
				m_vendor = "Xbox driver";
				m_renderer = "Xbox NV2A";
				lstrcpy(m_version, "8.0");
			}
			if ( m_textureState.GetMaxStages() > 1 ) 
			{
				m_extensions = " GL_SGIS_multitexture GL_EXT_texture_object ";
			}
			else 
			{
				m_extensions = " GL_EXT_texture_object ";
			}
		}
	}

	GLint CompatibleTextureComponents(GLint internalformat)
	{
		switch (internalformat)
		{
		case GL_RGB:
		case GL_RGB5:
		case GL_RGB8:
			return 3;
		case GL_RGBA:
		case GL_RGBA4:
		case GL_RGBA8:
		case GL_RGB5_A1:
			return 4;
		default:
			return internalformat;
		}
	}

	D3DFORMAT GLToDXPixelFormat(GLint internalformat, GLenum format)
	{
		D3DFORMAT d3dFormat = D3DFMT_UNKNOWN;
		if (internalformat == 0x9997) // GL_DDS_RGB16_EXT
		{
			return D3DFMT_R5G6B5;
		}
		if (internalformat == 0x9998) // GL_DDS_RGBA32_EXT
		{
			return D3DFMT_A8R8G8B8;
		}
		internalformat = CompatibleTextureComponents(internalformat);
		if ( g_force16bitTextures ) 
		{
			switch ( format ) 
			{
			case GL_RGBA:
				switch ( internalformat ) 
				{
				default:
				case 4:
//					d3dFormat = D3DFMT_A1R5G5B5; break;
					d3dFormat = D3DFMT_A4R4G4B4; break;
				case 3:
					d3dFormat = D3DFMT_R5G6B5; break;
				}
				break;
			case GL_COLOR_INDEX: d3dFormat = D3DFMT_P8; break;
			case GL_LUMINANCE: d3dFormat = D3DFMT_L8; break;
			case GL_ALPHA: d3dFormat = D3DFMT_A8; break;
			case GL_INTENSITY: d3dFormat = D3DFMT_L8; break;
			case GL_RGBA4: d3dFormat = D3DFMT_A4R4G4B4; break;
			default:
				InterpretError(E_FAIL);
			}
		}
		else 
		{
			// for
			switch ( format ) 
			{
			case GL_RGBA:
				switch ( internalformat ) 
				{
				default:
				case 4:
					d3dFormat = D3DFMT_A8R8G8B8; break;
				case 3:
					d3dFormat = D3DFMT_X8R8G8B8; break;
				}
				break;
			case GL_COLOR_INDEX: d3dFormat = D3DFMT_P8; break;
			case GL_LUMINANCE: d3dFormat = D3DFMT_L8; break;
			case GL_ALPHA: d3dFormat = D3DFMT_A8; break;
			case GL_INTENSITY: d3dFormat = D3DFMT_L8; break;
			case GL_RGBA4: d3dFormat = D3DFMT_A4R4G4B4; break;
			default:
				InterpretError(E_FAIL);
			}
		}
		return d3dFormat;
	}

// Avoid warning 4061, enumerant 'foo' in switch of enum 'bar' is not explicitly handled by a case label.
#pragma warning( push )
#pragma warning( disable : 4061)

		HRESULT ConvertToCompatablePixels(GLint internalformat,
		GLsizei width, GLsizei height,
		GLenum /* format */, GLenum type,
		D3DFORMAT dxPixelFormat,
		const GLvoid *pixels, char**  compatablePixels,
		DWORD* newPitch){
		HRESULT hr = S_OK;
		if ( type != GL_UNSIGNED_BYTE ) 
		{
			return E_FAIL;
		}
		internalformat = CompatibleTextureComponents(internalformat);
		switch ( dxPixelFormat )
		{
		default:
			LocalDebugBreak();
			break;
		case D3DFMT_P8:
		case D3DFMT_L8:
		case D3DFMT_A8:
			{
				char* copy = StickyAlloc(width*height);
				memcpy(copy,pixels,width * height);
				*compatablePixels = copy;
				if ( newPitch )
					*newPitch = width;
			}
			break;
		case D3DFMT_A4R4G4B4:
			{
				int textureElementSize = 2;
				const unsigned char* glpixels = (const unsigned char*) pixels;
				char* dxpixels = StickyAlloc(textureElementSize * width * height);
				switch ( internalformat )
				{
				default:
					LocalDebugBreak();
					break;
				case 1:
					{
						for(int y = 0; y < height; y++)
						{
							for(int x = 0; x < width; x++)
							{
								unsigned short* dp = (unsigned short*) (dxpixels + (y*width+x)*textureElementSize);
								const unsigned char* sp = glpixels + (y*width+x);
								unsigned short v;
								unsigned short s = 0xf & (sp[0] >> 4);
								v = s; // blue
								v |= s << 4; // green
								v |= s << 8; // red
								v |= s << 12; // alpha
								*dp = v;
							}
						}
					}
					break;
				case 3:
					{
						for(int y = 0; y < height; y++)
						{
							for(int x = 0; x < width; x++)
							{
								unsigned short* dp = (unsigned short*) (dxpixels + (y*width+x)*textureElementSize);
								const unsigned char* sp = glpixels + (y*width+x)*4;
								unsigned short v;
								v = (0xf & (sp[2] >> 4)); // blue
								v |= (0xf & (sp[1] >> 4)) << 4; // green
								v |= (0xf & (sp[0] >> 4)) << 8; // red
								v |= 0xf000; // alpha
								*dp = v;
							}
						}
					}
					break;
				case 4:
					{
						for(int y = 0; y < height; y++)
						{
							for(int x = 0; x < width; x++)
							{
								unsigned short* dp = (unsigned short*)(dxpixels + (y*width+x)*textureElementSize);
								const unsigned char* sp = glpixels + (y*width+x)*4;
								unsigned short v;
								v = (0xf & (sp[2] >> 4)); // blue
								v |= (0xf & (sp[1] >> 4)) << 4; // green
								v |= (0xf & (sp[0] >> 4)) << 8; // red
								v |= (0xf & (sp[3] >> 4)) << 12; // alpha
								*dp = v;
							}
						}
					}
					break;
				}
				*compatablePixels = dxpixels;
				if ( newPitch )
					*newPitch = 2 * width;
			}
			break;
		case D3DFMT_R5G6B5:
			{
				int textureElementSize = 2;
				const char* glpixels = (const char*) pixels;
				char* dxpixels = StickyAlloc(textureElementSize * width * height);
				switch ( internalformat ) 
				{
				default:
					LocalDebugBreak();
					break;
				case 1:
					{
						for(int y = 0; y < height; y++){
							for(int x = 0; x < width; x++){
								unsigned short* dp = (unsigned short*) (dxpixels + (y*width+x)*textureElementSize);
								const char* sp = glpixels + (y*width+x);
								unsigned short v;
								v = (0x1f & (sp[0] >> 3)); // blue
								v |= (0x3f & (sp[0] >> 2)) << 5; // green
								v |= (0x1f & (sp[0] >> 3)) << 11; // red
								*dp = v;
							}
						}
					}
					break;
				case 3:
					{
						for(int y = 0; y < height; y++)
						{
							for(int x = 0; x < width; x++)
							{
								unsigned short* dp = (unsigned short*) (dxpixels + (y*width+x)*textureElementSize);
								const char* sp = glpixels + (y*width+x)*4;
								unsigned short v;
								v = (0x1f & (sp[2] >> 3)); // blue
								v |= (0x3f & (sp[1] >> 2)) << 5; // green
								v |= (0x1f & (sp[0] >> 3)) << 11; // red
								*dp = v;
							}
						}
					}
					break;
				case 4:
					{
						for(int y = 0; y < height; y++)
						{
							for(int x = 0; x < width; x++)
							{
								unsigned short* dp = (unsigned short*) (dxpixels + (y*width+x)*textureElementSize);
								const char* sp = glpixels + (y*width+x)*4;
								unsigned short v;
								v = (0x1f & (sp[2] >> 3)); // blue
								v |= (0x3f & (sp[1] >> 2)) << 5; // green
								v |= (0x1f & (sp[0] >> 3)) << 11; // red
								*dp = v;
							}
						}
					}
					break;
				}
				*compatablePixels = dxpixels;
				if ( newPitch )
					*newPitch = 2 * width;
			}
			break;
		case D3DFMT_X1R5G5B5:
			{
				int textureElementSize = 2;
				const char* glpixels = (const char*) pixels;
				char* dxpixels = StickyAlloc(textureElementSize * width * height);
				switch ( internalformat ) 
				{
				default:
					LocalDebugBreak();
					break;
				case 1:
					{
#define RGBTOR5G5B5(R, G, B) (0x8000 |  (0x1f & ((B) >> 3)) | ((0x1f & ((G) >> 3)) << 5) | ((0x1f & ((R) >> 3)) << 10))
#define Y5TOR5G5B5(Y) (0x8000 | ((Y) << 10) | ((Y) << 5) | (Y))
						static const unsigned short table[32] = {
							Y5TOR5G5B5(0), Y5TOR5G5B5(1), Y5TOR5G5B5(2), Y5TOR5G5B5(3),
							Y5TOR5G5B5(4), Y5TOR5G5B5(5), Y5TOR5G5B5(6), Y5TOR5G5B5(7),
							Y5TOR5G5B5(8), Y5TOR5G5B5(9), Y5TOR5G5B5(10), Y5TOR5G5B5(11),
							Y5TOR5G5B5(12), Y5TOR5G5B5(13), Y5TOR5G5B5(14), Y5TOR5G5B5(15),
							Y5TOR5G5B5(16), Y5TOR5G5B5(17), Y5TOR5G5B5(18), Y5TOR5G5B5(19),
							Y5TOR5G5B5(20), Y5TOR5G5B5(21), Y5TOR5G5B5(22), Y5TOR5G5B5(23),
							Y5TOR5G5B5(24), Y5TOR5G5B5(25), Y5TOR5G5B5(26), Y5TOR5G5B5(27),
							Y5TOR5G5B5(28), Y5TOR5G5B5(29), Y5TOR5G5B5(30), Y5TOR5G5B5(31)
						};
						unsigned short* dp = (unsigned short*) dxpixels;
						const unsigned char* sp = (const unsigned char*) glpixels;
						int numPixels = height * width;
						int i = numPixels >> 2;
						while(i > 0) {
							*dp++ = table[(*sp++) >> 3];
							*dp++ = table[(*sp++) >> 3];
							*dp++ = table[(*sp++) >> 3];
							*dp++ = table[(*sp++) >> 3];
							--i;
						}

						i = numPixels & 3;
						while(i > 0) {
							*dp++ = table[(*sp++) >> 3];
							--i;
						}
					}
					break;
				case 3:
					{
						for(int y = 0; y < height; y++)
						{
							for(int x = 0; x < width; x++)
							{
								unsigned short* dp = (unsigned short*) (dxpixels + (y*width+x)*textureElementSize);
								const unsigned char* sp = (const unsigned char*) glpixels + (y*width+x)*4;
								unsigned short v;
								v = (sp[2] >> 3); // blue
								v |= (sp[1] >> 3) << 5; // green
								v |= (sp[0] >> 3) << 10; // red
								v |= 0x8000; // alpha
								*dp = v;
							}
						}
					}
					break;
				case 4:
					{
						for(int y = 0; y < height; y++)
						{
							for(int x = 0; x < width; x++)
							{
								unsigned short* dp = (unsigned short*) (dxpixels + (y*width+x)*textureElementSize);
								const unsigned char* sp = (const unsigned char*) glpixels + (y*width+x)*4;
								unsigned short v;
								v = (sp[2] >> 3); // blue
								v |= (sp[1] >> 3) << 5; // green
								v |= (sp[0] >> 3) << 10; // red
								v |= 0x8000; // alpha
								*dp = v;
							}
						}
					}
					break;
				}
				*compatablePixels = dxpixels;
				if ( newPitch ) {
					*newPitch = 2 * width;
				}
			}
			break;
		case D3DFMT_A1R5G5B5:
			{
				int textureElementSize = 2;
				const char* glpixels = (const char*) pixels;
				char* dxpixels = StickyAlloc(textureElementSize * width * height);
				switch ( internalformat ) 
				{
				default:
					LocalDebugBreak();
					break;
				case 1:
					{
						for(int y = 0; y < height; y++){
							for(int x = 0; x < width; x++){
								unsigned short* dp = (unsigned short*) (dxpixels + (y*width+x)*textureElementSize);
								const char* sp = glpixels + (y*width+x);
								unsigned short v;
								v = (0x1f & (sp[0] >> 3)); // blue
								v |= (0x1f & (sp[0] >> 3)) << 5; // green
								v |= (0x1f & (sp[0] >> 3)) << 10; // red
								v |= (0x01 & (sp[0] >> 7)) << 15; // alpha
								*dp = v;
							}
						}
					}
					break;
				case 3:
					{
						for(int y = 0; y < height; y++){
							for(int x = 0; x < width; x++){
								unsigned short* dp = (unsigned short*) (dxpixels + (y*width+x)*textureElementSize);
								const char* sp = glpixels + (y*width+x)*4;
								unsigned short v;
								v = (0x1f & (sp[2] >> 3)); // blue
								v |= (0x1f & (sp[1] >> 3)) << 5; // green
								v |= (0x1f & (sp[0] >> 3)) << 10; // red
								v |= 0x8000; // alpha
								*dp = v;
							}
						}
					}
					break;
				case 4:
					{
						for(int y = 0; y < height; y++)
						{
							for(int x = 0; x < width; x++)
							{
								unsigned short* dp = (unsigned short*) (dxpixels + (y*width+x)*textureElementSize);
								const char* sp = glpixels + (y*width+x)*4;
								unsigned short v;
								v = (0x1f & (sp[2] >> 3)); // blue
								v |= (0x1f & (sp[1] >> 3)) << 5; // green
								v |= (0x1f & (sp[0] >> 3)) << 10; // red
								v |= (0x01 & (sp[3] >> 7)) << 15; // alpha
								*dp = v;
							}
						}
					}
					break;
				}
				*compatablePixels = dxpixels;
				if ( newPitch ) {
					*newPitch = 2 * width;
				}
			}
			break;
		case D3DFMT_X8R8G8B8:
		case D3DFMT_A8R8G8B8:
			{
				int textureElementSize = 4;
				const char* glpixels = (const char*) pixels;
				char* dxpixels = StickyAlloc(textureElementSize * width * height);
				switch ( internalformat )
				{
				default:
					LocalDebugBreak();
					break;
				case 1:
					{
						for(int y = 0; y < height; y++){
							for(int x = 0; x < width; x++){
								char* dp = dxpixels + (y*width+x)*textureElementSize;
								const char* sp = glpixels + (y*width+x);
								dp[0] = sp[0]; // blue
								dp[1] = sp[0]; // green
								dp[2] = sp[0]; // red
								dp[3] = sp[0];
							}
						}
					}
					break;
				case 3:
					{
						for(int y = 0; y < height; y++){
							for(int x = 0; x < width; x++){
								unsigned char* dp = (unsigned char*) dxpixels + (y*width+x)*textureElementSize;
								const unsigned char* sp = (unsigned char*) glpixels + (y*width+x)*4;
								dp[0] = sp[2]; // blue
								dp[1] = sp[1]; // green
								dp[2] = sp[0]; // red
								dp[3] = 0xff;
							}
						}
					}
					break;
				case 4:
					{
						for(int y = 0; y < height; y++)
						{
							for(int x = 0; x < width; x++)
							{
								char* dp = dxpixels + (y*width+x)*textureElementSize;
								const char* sp = glpixels + (y*width+x)*4;
								dp[0] = sp[2]; // blue
								dp[1] = sp[1]; // green
								dp[2] = sp[0]; // red
								dp[3] = sp[3]; // alpha
							}
						}
					}
					break;
				}
				*compatablePixels = dxpixels;
				if ( newPitch ) 
					*newPitch = 4 * width;
			}
		}
		return hr;
	}
#pragma warning( pop )
};

void /*APIENTRY*/ glAlphaFunc (GLenum func, GLclampf ref)
{
	gFakeGL->glAlphaFunc(func, ref);
}

void /*APIENTRY*/ glBegin (GLenum mode)
{
	gFakeGL->glBegin(mode);
}

void /*APIENTRY*/ glBlendFunc (GLenum sfactor, GLenum dfactor)
{
	gFakeGL->glBlendFunc(sfactor, dfactor);
}

void /*APIENTRY*/ glClear (GLbitfield mask)
{
	gFakeGL->glClear(mask);
}

void /*APIENTRY*/ glClearColor (GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
	gFakeGL->glClearColor(red, green, blue, alpha);
}

void /*APIENTRY*/ glColor3f (GLfloat red, GLfloat green, GLfloat blue)
{
	gFakeGL->glColor3f(red, green, blue);
}

void /*APIENTRY*/ glColor3ubv (const GLubyte *v)
{
	gFakeGL->glColor3ubv(v);
}

void /*APIENTRY*/ glColor4ubv (const GLubyte *v)
{
	gFakeGL->glColor4ubv(v);
}

void /*APIENTRY*/ glColor4f (GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
	gFakeGL->glColor4f(red, green, blue, alpha);
}

void /*APIENTRY*/ glColor4ub (GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha)
{
	gFakeGL->glColor4ub(red, green, blue, alpha);
}

void /*APIENTRY*/ glColor4fv (const GLfloat *v)
{
	gFakeGL->glColor4fv(v);
}

extern "C" void JkaFakeglClipPlane0( const GLdouble *equation )
{
	if (gFakeGL)
	{
		gFakeGL->glClipPlane0(equation);
	}
}

extern "C" void JkaFakeglScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
	if (gFakeGL)
	{
		gFakeGL->glScissor(x, y, width, height);
	}
}

void /*APIENTRY*/ glCullFace (GLenum mode)
{
	gFakeGL->glCullFace(mode);
}

void /*APIENTRY*/ glDepthFunc (GLenum func)
{
	gFakeGL->glDepthFunc(func);
}

void /*APIENTRY*/ glDepthMask (GLboolean flag)
{
	gFakeGL->glDepthMask(flag);
}

void /*APIENTRY*/ glDepthRange (GLclampd zNear, GLclampd zFar)
{
	gFakeGL->glDepthRange(zNear, zFar);
}

void /*APIENTRY*/ glDisable (GLenum cap)
{
	gFakeGL->glDisable(cap);
}

void /*APIENTRY*/ glDrawBuffer (GLenum mode)
{
	gFakeGL->glDrawBuffer(mode);
}

void /*APIENTRY*/ glEnable (GLenum cap)
{
	gFakeGL->glEnable(cap);
}

void /*APIENTRY*/ glEnd (void)
{
	return; // Does nothing
//	gFakeGL->glEnd();
}

void /*APIENTRY*/ glFinish (void)
{
	gFakeGL->glFinish();
}

extern "C" void JkaFakeglFogf(GLenum pname, GLfloat param)
{
	if (gFakeGL)
	{
		gFakeGL->glFogf(pname, param);
	}
}

extern "C" void JkaFakeglFogfv(GLenum pname, const GLfloat *params)
{
	if (gFakeGL)
	{
		gFakeGL->glFogfv(pname, params);
	}
}

extern "C" void JkaFakeglFogi(GLenum pname, GLint param)
{
	if (gFakeGL)
	{
		gFakeGL->glFogi(pname, param);
	}
}

void /*APIENTRY*/ glFrustum (GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar)
{
	gFakeGL->glFrustum(left, right, bottom, top, zNear, zFar);
}

void /*APIENTRY*/ glGetFloatv (GLenum pname, GLfloat *params)
{
	gFakeGL->glGetFloatv(pname, params);
}

const GLubyte* /*APIENTRY*/ glGetString (GLenum name)
{
	return gFakeGL->glGetString(name);
}

void /*APIENTRY*/ glHint (GLenum target, GLenum mode)
{
	gFakeGL->glHint(target, mode);
}

GLboolean /*APIENTRY*/ glIsEnabled(GLenum cap)
{
	return gFakeGL->glIsEnabled(cap);
}

void /*APIENTRY*/ glLoadIdentity (void)
{
	gFakeGL->glLoadIdentity();
}

void /*APIENTRY*/ glLoadMatrixf (const GLfloat *m)
{
	gFakeGL->glLoadMatrixf(m);
}

void /*APIENTRY*/ glMatrixMode (GLenum mode)
{
	gFakeGL->glMatrixMode(mode);
}

void /*APIENTRY*/  glOrtho (GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar)
{
	gFakeGL->glOrtho(left, right, top, bottom, zNear, zFar);
}

void /*APIENTRY*/ glPolygonMode (GLenum face, GLenum mode)
{
	gFakeGL->glPolygonMode(face, mode);
}

void /*APIENTRY*/ glPopMatrix (void)
{
	gFakeGL->glPopMatrix();
}

void /*APIENTRY*/ glPushMatrix (void)
{
	gFakeGL->glPushMatrix();
}

void /*APIENTRY*/ glReadBuffer (GLenum mode)
{
	gFakeGL->glReadBuffer(mode);
}

void /*APIENTRY*/glReadPixels (GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels)
{
	gFakeGL->glReadPixels(x, y, width, height, format, type, pixels);
}

void /*APIENTRY*/ glRotatef (GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
	gFakeGL->glRotatef(angle, x, y, z);
}

void /*APIENTRY*/ glScalef (GLfloat x, GLfloat y, GLfloat z)
{
	gFakeGL->glScalef(x, y, z);
}

void /*APIENTRY*/ glShadeModel (GLenum mode)
{
	gFakeGL->glShadeModel(mode);
}

void /*APIENTRY*/ glTexCoord2f (GLfloat s, GLfloat t)
{
	gFakeGL->glTexCoord2f(s, t);
}

void /*APIENTRY*/ glTexEnvf (GLenum target, GLenum pname, GLfloat param)
{
	gFakeGL->glTexEnvf(target, pname, param);
}

void /*APIENTRY*/ glTexImage2D (GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
	gFakeGL->glTexImage2D(target, level, internalformat, width, height, border, format, type, pixels);
}

void /*APIENTRY*/ glTexParameterf (GLenum target, GLenum pname, GLfloat param)
{
	gFakeGL->glTexParameterf(target, pname, param);
}

void /*APIENTRY*/ glTexSubImage2D (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels)
{
	gFakeGL->glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels);
}

void /*APIENTRY*/ glTranslatef (GLfloat x, GLfloat y, GLfloat z)
{
	gFakeGL->glTranslatef(x, y, z);
}

void /*APIENTRY*/ glVertex2f (GLfloat x, GLfloat y)
{
	gFakeGL->glVertex2f(x, y);
}

void /*APIENTRY*/ glVertex3f (GLfloat x, GLfloat y, GLfloat z)
{
	gFakeGL->glVertex3f(x, y, z);
}

void /*APIENTRY*/ glVertex3fv (const GLfloat *v)
{
	gFakeGL->glVertex3fv(v);
}

void /*APIENTRY*/ glViewport (GLint x, GLint y, GLsizei width, GLsizei height)
{
	gFakeGL->glViewport(x, y, width, height);
}

extern "C" int JkaFakeglUploadDDS(GLint internalformat, GLsizei width, GLsizei height,
	GLint mipcount, const GLvoid *pixels, DWORD pixelBytes)
{
	if (!gFakeGL)
	{
		return 0;
	}
	return gFakeGL->UploadDDSTexture(internalformat, width, height,
		mipcount, pixels, pixelBytes) ? 1 : 0;
}

extern "C" void JkaFakeglDeleteTexture(GLuint texture)
{
	if (!gFakeGL)
		return;
	gFakeGL->DeleteTexture(texture);
}

extern "C" int JkaFakeglIsTexture(GLuint texture)
{
	if (!gFakeGL)
		return 0;
	return gFakeGL->IsTexture(texture) ? 1 : 0;
}

#ifdef _XBOX
extern "C" int JkaFakeglDrawIndexedPrimitiveUP(D3DPRIMITIVETYPE dptPrimitiveType, DWORD typeDesc,
	UINT vertexCount, UINT primitiveCount, const void *indices,
	const void *vertices, UINT stride, int stefxOverlayActive,
	int stefxOverlayHud, int stefxOverlayBeam)
{
	if (!gFakeGL)
	{
		return 0;
	}
	return gFakeGL->DrawIndexedPrimitiveUPXbox(dptPrimitiveType, typeDesc,
		vertexCount, primitiveCount, indices, vertices, stride,
		stefxOverlayActive, stefxOverlayHud, stefxOverlayBeam) ? 1 : 0;
}

extern "C" void JkaFakeglSetEliteForceScriptPanelDrawContext(int active)
{
	if (!gFakeGL)
	{
		return;
	}
	gFakeGL->SetEliteForceScriptPanelDrawContext(active);
}
#endif

//HDC gHDC;
//HGLRC gHGLRC;

HGLRC /*APIENTRY*/ wglCreateContext(/*maindc*/)
{
	/*return (HGLRC)*/gFakeGL = new FakeGL(/*mainwindow*/);

	// We don't return a handle on XBox
	if(!gFakeGL)
		return (HGLRC)0; 

	return (HGLRC)1;
}

BOOL /*WINAPI*/ wglDeleteContext(/*HGLRC hglrc*/)
{
	FakeGL* pFakeGL = gFakeGL;//(FakeGL*) hglrc;
	delete pFakeGL;
	
    pFakeGL = NULL;
	return true;
}

/*
HGLRC WINAPI wglGetCurrentContext(VOID)
{
	return gHGLRC;
}

HDC   WINAPI wglGetCurrentDC(VOID)
{ 
	return gHDC;
}
*/

void /*APIENTRY*/ glBindTexture(GLenum target, GLuint texture)
{
	gFakeGL->glBindTexture(target, texture);
}

static void /*APIENTRY*/ BindTextureExt(GLenum target, GLuint texture)
{
	gFakeGL->glBindTexture(target, texture);
}

static void /*APIENTRY*/ MTexCoord2fSGIS(GLenum target, GLfloat s, GLfloat t)
{
	gFakeGL->glMTexCoord2fSGIS(target, s, t);
}

static void /*APIENTRY*/ SelectTextureSGIS(GLenum target)
{
	gFakeGL->glSelectTextureSGIS(target);
}

extern "C" void JkaFakeglMTexCoord2fSGIS(GLenum target, GLfloat s, GLfloat t)
{
	gFakeGL->glMTexCoord2fSGIS(target, s, t);
}

extern "C" void JkaFakeglSelectTextureSGIS(GLenum target)
{
	gFakeGL->glSelectTextureSGIS(target);
}

extern "C" GLboolean JkaFakeglIsEnabled(GLenum cap)
{
	return gFakeGL->glIsEnabled(cap);
}

extern "C" void JkaFakeglEnable(GLenum cap)
{
	gFakeGL->glEnable(cap);
}

extern "C" void JkaFakeglDisable(GLenum cap)
{
	gFakeGL->glDisable(cap);
}

// Type cast unsafe conversion from 
#pragma warning( push )
#pragma warning( disable : 4191)

PROC /*APIENTRY*/ wglGetProcAddress(LPCSTR s)
{
	static LPCSTR kBindTextureEXT = "glBindTextureEXT";
	static LPCSTR kMTexCoord2fSGIS = "glMTexCoord2fSGIS"; // Multitexture
	static LPCSTR kSelectTextureSGIS = "glSelectTextureSGIS";
	if ( strncmp(s, kBindTextureEXT, sizeof(kBindTextureEXT)-1) == 0)
	{
		return (PROC) BindTextureExt;
	}
	else if ( strncmp(s, kMTexCoord2fSGIS, sizeof(kMTexCoord2fSGIS)-1) == 0)
	{
		return (PROC) MTexCoord2fSGIS;
	}
	else if ( strncmp(s, kSelectTextureSGIS, sizeof(kSelectTextureSGIS)-1) == 0)
	{
		return (PROC) SelectTextureSGIS;
	}
	// LocalDebugBreak();
	return 0;
}

#pragma warning( pop )

BOOL /*WINAPI*/ wglMakeCurrent(/*HDC hdc, HGLRC hglrc*/)
{
	// Pointer assigned in CreateContext
	/* 
	gHDC = hdc;
	gHGLRC = hglrc;
	gFakeGL = (FakeGL*) hglrc;
	*/
	if(!gFakeGL)
		return FALSE;

	return TRUE;
}

void d3dEvictTextures()
{
	gFakeGL->EvictTextures();
}

int d3dIsResolutionHD()
{	
	// Check if we have component cables
	if((XGetAVPack() == XC_AV_PACK_HDTV) && (XGetVideoFlags() & XC_VIDEO_FLAGS_HDTV_720p))
		return TRUE;

	return FALSE;
}

void d3dSetMode(int width, int height, int bpp, int zbpp, int vmode)
{
	gWidth = width;
	gHeight = height;
	gVideoMode = vmode;
}

void /*WINAPI*/ FakeSwapBuffers()
{
	if (!gFakeGL)
		return;

	gFakeGL->SwapBuffers();
}

void d3dSetGammaRamp(const unsigned char* gammaTable)
{
	gFakeGL->SetGammaRamp(gammaTable);
}

void d3dInitSetForce16BitTextures(int force16bitTextures)
{
	// Called before gFakeGL exits. That's why we set a global
	g_force16bitTextures = force16bitTextures != 0; 
}

void d3dHint_GenerateMipMaps(int value)
{
	gFakeGL->Hint_GenerateMipMaps(value);
}

float d3dGetD3DDriverVersion()
{
	return 0.81f;
}
