// Simple header file to dispatch to the relevant platform API headers
#ifndef _PLATFORM_H
#define _PLATFORM_H

#if defined(_XBOX)
#define NOD3D
#define NODSOUND
#include <xtl.h>
#undef NODSOUND
#undef NOD3D
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN 1
#endif

#if defined(_WINDOWS)
#include <windows.h>
#endif

#endif
