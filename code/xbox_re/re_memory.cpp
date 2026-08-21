/*
 * RE notes for the shipping Jedi Academy MP Xbox memory contract.
 *
 * Authority: retail jamp.xbe, XDK 5558.
 * Com_InitZoneMemory is the 413-byte function at VA 0x0004E240.
 *
 * Allocation order recovered from the retail machine code:
 *   0x0004E293: D3D_AllocContiguousMemory(0x00A00000, 0)
 *   0x0004E29E: D3D_AllocContiguousMemory(0x00400000, 0)
 *   0x0004E2EB: query available physical memory
 *   0x0004E2F4: zone size = available - 0x00C55020
 *   0x0004E2FC: GlobalAlloc(0, zone size)
 *
 * Only the two GPU texture pools use contiguous memory. The general zone uses
 * cached heap memory. D3D contiguous allocations are write-combined on Xbox,
 * so placing read-heavy BSP, model, collision, or game data there is a severe
 * CPU performance error. See notes/ja_mp_retail_renderer_re_2026-08-14.md.
 */

#include "../game/q_shared.h"
#include "../qcommon/qcommon.h"

#ifdef _XBOX
#include <xtl.h>

/* Link-time marker for the recovered allocation contract. */
void RE_Memory_ZoneAlloc_Verify( void )
{
}

/*
 * Retail Z_Malloc retains the VV linked-list allocator shape used by the
 * source tree: short-lived tags allocate from the end, permanent tags from
 * the beginning, and headers track size, alignment, and tag ownership.
 */
void RE_Memory_ZoneMalloc_Verify( void )
{
    extern void *Z_Malloc( int size, memtag_t tag, qboolean zeroed, int alignment );
    void *p = Z_Malloc( 4, TAG_STATIC, qtrue, 4 );
    if ( p )
    {
        Z_Free( p );
    }
}

#endif /* _XBOX */
