/*
 * Local Jedi-Academy-X Xbox D3D8 shim.
 *
 * Under the OpenJKDF2-pattern build (XDK 5558 d3d8 surgical override), the
 * fat 5558 d3d8.h has all D3D types AND extension types inline.  The
 * separate d3d8types-xbox.h was a 5849-split-design artifact — it doesn't
 * exist in 5558 and isn't needed.
 *
 * Previous version of this file fabricated several types (D3DVBLANKDATA,
 * D3DSWAPDATA, D3DPIXELSHADERDEF, D3DCALLBACKTYPE, D3DMEMORY,
 * D3DCOPYRECTSTATE, D3DCOPYRECTROPSTATE) plus #defines (D3DTSS_MAXSTAGES,
 * D3DTSS_MAX, D3DRS_MAX, D3DTS_MAX, D3DVS_STREAMS_MAX_V1_0,
 * D3DPS_CONSTREG_MAX_DX8, D3DVS_CONSTREG_COUNT_XBOX, plus PRESENTFLAG_*).
 * All of these are present in 5558's d3d8.h / d3d8types.h, which we now use.
 *
 * This file simply forwards to <d3d8.h> via include-path resolution.
 * That picks up our 5558 surgical override copy (code/win32/d3d8.h) which
 * has the full Xbox D3D type surface inline.
 */

#ifndef _D3D8TYPES_XBOX_H_
#define _D3D8TYPES_XBOX_H_

#include <d3d8.h>

#endif
