/*
 * Local project override for Xbox D3D8 includes.
 *
 * Under the OpenJKDF2-pattern build (XDK 5558 primary), <d3d8.h> resolves
 * to the FAT 2757-line variant in xbox\public\xdk\inc\d3d8.h, which has
 * all Xbox D3D extensions (IDirect3DPalette8, D3DDevice_*, etc.) inline.
 * No separate D3D8-Xbox.h exists in 5558 and none is needed.
 *
 * The previous version of this shim hardcoded the absolute path
 * "C:/XDK/xbox/include/D3D8-Xbox.h" — which forced inclusion of the 5849
 * split-design header that redirects API names to 5849-only symbols
 * (e.g. D3DDevice::GetBackBuffer → D3DDevice_GetBackBuffer2).  Those
 * symbols are absent from the 5558 retail d3d8.lib, breaking the link.
 *
 * Including <d3d8.h> here without an absolute path lets include-path
 * resolution find whichever d3d8.h the build script puts first — which
 * is 5558's fat variant.
 */

#ifndef _LOCAL_JA_D3D8_XBOX_H_
#define _LOCAL_JA_D3D8_XBOX_H_

#include <d3d8.h>

#endif
