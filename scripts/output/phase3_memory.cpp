/*
 * phase3_memory.cpp
 * RE Phase 3 — Memory subsystem reconstruction
 * Source: default.xbe (Jedi Academy Xbox, XDK 5558)
 *
 * Functions reconstructed from binary analysis (phase3_memory.txt).
 *
 * Key binary addresses:
 *   Com_InitZoneMemory   0x49945   (107 insns; calls D3D_AllocContiguousMemory)
 *   Z_Malloc             0x49B00   (linked-list allocator)
 *   PhysRAM query        0xC2582   (calls GlobalMemoryStatus via [0x2CD0EC])
 *   D3D_AllocContiguous  0x23F620  (D3D section — allocates pool)
 *   Zone header alloc    0xC268B   (allocates ZoneFreeBlock from pool start)
 *   NtCreateSemaphore wr 0xC1A23   (mutex/semaphore for Z_ thread safety)
 *   Sound pool alloc     0xEC6D0   (allocates 0x139000 bytes for sound pool)
 *
 * *** BINARY DIFFERS FROM SOURCE ***
 *
 * Source (code/qcommon/z_memman_console.cpp) uses:
 *   VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)
 *
 * Shipped binary uses:
 *   D3D_AllocContiguousMemory(0x1000000, 0)   — fixed 16MB, page-aligned
 *
 * Both ultimately call NtAllocateVirtualMemory (kernel thunk ord_183), so
 * the memory is equivalent.  D3D_AllocContiguousMemory additionally ensures
 * physical contiguity, which is beneficial for DMA.
 *
 * FIXED SIZE: Binary always allocates exactly 0x1000000 (16MB) regardless of
 * available RAM.  Source probes GlobalMemoryStatus and subtracts a headroom
 * constant.  With 64MB of Xbox RAM and texture pool (16MB) + sound pool
 * (1.3MB) + framebuffer reserved, 16MB is a reasonable fixed zone size.
 *
 * SOURCE PATCH: See landing in code/qcommon/z_memman_console.cpp —
 *   VirtualAlloc replaced with D3D_AllocContiguousMemory(ZONE_POOL_SIZE, 0).
 */

#ifdef RE_PHASE3_MEMORY_COMPILE

#include "../game/q_shared.h"
#include "../qcommon/qcommon.h"
#include "../win32/xbox_texture_man.h"
#include <xtl.h>

/* Zone pool size as used by shipped binary: fixed 16MB */
#define ZONE_POOL_SIZE_BINARY   (16 * 1024 * 1024)   /* 0x1000000 */

/* Sound pool size as used by shipped binary (allocated by 0xEC6D0) */
#define SOUND_POOL_SIZE_BINARY  0x139000

/* External D3D allocator (D3D section @ 0x23F620).
   Prototype matches XDK 5558 internal D3D_AllocContiguousMemory. */
extern "C" void *D3D_AllocContiguousMemory( DWORD Size, DWORD Alignment );

/* =========================================================================
 * Com_InitZoneMemory  (binary @ 0x49945, 107 insns)
 *
 * Full reconstruction of what the shipped binary does:
 *
 *   1. Guard: if already initialized, return immediately.
 *   2. Com_Printf("Initialising zone memory .....\n")
 *   3. Clear stats table (0x100 bytes @ 0x7BC2F0)
 *      and free block overflow buffer (0x10000 bytes @ 0x7AC2F0)
 *   4. Query physical RAM: call GlobalMemoryStatus via [0x2CD0EC] (ord_181)
 *   5. Com_Printf("*** PhysRAM: %d used, %d free\n", used, free)
 *   6. Allocate 16MB contiguous pool:
 *        s_PoolBase = D3D_AllocContiguousMemory(0x1000000, 0)
 *        s_PoolSize = 0x1000000
 *        [0x81CE18] = s_PoolBase
 *        [0x81CE20] = 0x1000000
 *   7. Query RAM again (post-alloc)
 *   8. Call 0xC268B: set up first ZoneFreeBlock header at pool start
 *   9. Init jump table (64 entries, resolution = size/64 + 1)
 *  10. Set s_FreeStart, s_FreeEnd sentinel blocks
 *  11. s_Stats.m_CountFree = 1; s_Stats.m_SizeFree = size
 *  12. s_Initialized = true
 *  13. Register console commands: zone_stats, zone_details, zone_memmap, zone_cstats
 *  14. NtCreateSemaphore wrapper @ 0xC1A23 -> stores handle @ [0x30C878]
 *  15. Sound pool allocator @ 0xEC6D0 -> allocates SOUND_POOL_SIZE_BINARY bytes
 *
 * NOTE: Texture pool (gTextures.Initialize) is NOT called here in the binary.
 *       It must be called before Com_InitZoneMemory from a different site.
 *       In source: gTextures.Initialize is called inside Com_InitZoneMemory
 *       before the VirtualAlloc.  In binary: separate call site.
 * ========================================================================= */
void Com_InitZoneMemory_RE( void )
{
    static bool s_Initialized = false;
    if ( s_Initialized ) return;

    Com_Printf( "Initialising zone memory .....\n" );

    /* Clear stats and overflow buffer */
    extern void *s_Stats;       /* ZoneStats at 0x7BC2F0 */
    extern void *s_FreeOverflow;/* ZoneFreeBlock[4096] at 0x7AC2F0 */
    memset( s_Stats, 0, 0x100 );
    memset( s_FreeOverflow, 0, 0x10000 );

    /* Query physical RAM (ord_181 = NtGlobalData.dwPhysicalRam) */
    MEMORYSTATUS status;
    GlobalMemoryStatus( &status );
    Com_Printf( "*** PhysRAM: %d used, %d free\n",
        (int)(status.dwTotalPhys - status.dwAvailPhys),
        (int)status.dwAvailPhys );

    /* *** KEY DIFFERENCE FROM SOURCE ***
     * Binary: D3D_AllocContiguousMemory(0x1000000, 0) — fixed 16MB
     * Source: VirtualAlloc(NULL, size, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE)
     *         where size = dwAvailPhys - ZONE_HEAP_FREE, capped at 64MB
     *
     * Use D3D_AllocContiguousMemory to match the shipped binary exactly. */
    void *s_PoolBase = D3D_AllocContiguousMemory( ZONE_POOL_SIZE_BINARY, 0 );
    int   s_PoolSize = ZONE_POOL_SIZE_BINARY;

    /* Pool pointer and size stored at binary globals */
    /* g_zoneBase = s_PoolBase;   (binary @ [0x81CE18]) */
    /* g_zoneSize = s_PoolSize;   (binary @ [0x81CE20]) */

    /* Query RAM again post-allocation */
    GlobalMemoryStatus( &status );
    Com_Printf( "*** PhysRAM: %d used, %d free\n",
        (int)(status.dwTotalPhys - status.dwAvailPhys),
        (int)status.dwAvailPhys );

    /* Zone header / free block setup (0xC268B):
       First ZoneFreeBlock at pool start, jump table init. */
    /* Z_SetupFreeBlock(s_PoolBase, s_PoolSize); */

    /* s_Initialized = true; */

    /* Register console commands */
    Cmd_AddCommand( "zone_stats",   Z_Stats_f );
    Cmd_AddCommand( "zone_details", Z_Details_f );
    Cmd_AddCommand( "zone_memmap",  Z_DumpMemMap_f );
    Cmd_AddCommand( "zone_cstats",  Z_CompactStats );

    /* Mutex (NtCreateSemaphore wrapper @ 0xC1A23) */
    /* g_zoneMutex = CreateMutex(NULL, FALSE, NULL); */

    /* Sound pool (0xEC6D0): allocates SOUND_POOL_SIZE_BINARY bytes */
    /* S_AllocSoundPool(SOUND_POOL_SIZE_BINARY); */

    s_Initialized = true;
}

/* =========================================================================
 * Z_Malloc  (binary @ 0x49B00)
 *
 * Standard VV linked-list zone allocator:
 *   - Searches free list from s_FreeStart for first-fit block >= size
 *   - Delinks the free block, writes ZoneHeader, returns pointer past header
 *   - Updates s_Stats.m_CountAlloc ([0x7BC300]) and m_SizeAlloc ([0x7BC304])
 *   - Allocs at pool end for short-lived blocks (TAG_TEMP), start otherwise
 *
 * SOURCE: code/qcommon/z_memman_console.cpp Z_Malloc — MATCHES binary.
 * ========================================================================= */
void *Z_Malloc_RE( int size, memtag_t tag, qboolean /*zeroed*/ )
{
    /* Standard linked-list first-fit allocation — see z_memman_console.cpp */
    (void)size; (void)tag;
    return NULL; /* stub */
}

#endif /* RE_PHASE3_MEMORY_COMPILE */
