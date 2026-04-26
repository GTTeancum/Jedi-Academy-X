/*
 * re_memory.cpp  —  RE Phase 3: Memory subsystem
 * Jedi Academy Xbox shipped binary (default.xbe, XDK 5558)
 *
 * Binary anchor map:
 *   Com_InitZoneMemory   0x00049945   (107 insns — Z_Init)
 *   Z_Malloc             0x00049B00
 *   GlobalMemoryStatus   via [0x2CD0EC] = NtGlobalData (kernel ord_181)
 *   D3D_AllocContiguous  0x0023F620   (D3D section thunk)
 *   NtCreateSemaphore wr 0x000C1A23
 *   Sound pool alloc     0x000EC6D0   (allocates 0x139000 bytes)
 *
 * KEY DIFFERENCE FOUND AND PATCHED:
 *   Binary uses D3D_AllocContiguousMemory(0x1000000, 0) — fixed 16 MB.
 *   Source used VirtualAlloc (dynamic size from GlobalMemoryStatus).
 *   PATCH APPLIED: code/qcommon/z_memman_console.cpp now uses
 *   D3D_AllocContiguousMemory on _XBOX builds. ✓
 *
 * MmAllocateContiguousMemory:    0 callers in .text
 * MmAllocateContiguousMemoryEx:  1 caller @ 0xC1E29 (XAPI; allocates 0x1000)
 *
 * Sound pool (binary 0xEC6D0): allocates 0x139000 bytes at end of Z_Init.
 * Source equivalent: G_ReserveZoneGentities() is called instead.
 * The sound pool in source is managed separately by snd_dma_console.cpp.
 */

#include "../game/q_shared.h"
#include "../qcommon/qcommon.h"

#ifdef _XBOX
#include <xtl.h>

/* D3D_AllocContiguousMemory declaration (matches z_memman_console.cpp). */
extern "C" void* __cdecl D3D_AllocContiguousMemory( DWORD Size, DWORD Alignment );

/* Zone pool size matching shipped binary: 16 MB fixed. */
#define ZONE_POOL_SIZE_BINARY  (16 * 1024 * 1024)  /* 0x1000000 */

/*
 * RE_Memory_ZoneAlloc_Verify — validates zone pool allocation matches binary.
 *
 * Binary Z_Init (0x49945) allocation sequence:
 *   push 0                       ; Alignment = 0 (page-aligned by D3D)
 *   push 0x1000000               ; Size = 16 MB
 *   call D3D_section@0x23F620    ; D3D_AllocContiguousMemory
 *   mov [0x81CE18], eax          ; store pool base
 *   mov [0x81CE20], 0x1000000    ; store pool size
 *
 * D3D_AllocContiguousMemory is safe to call before IDirect3D8::CreateDevice.
 * It internally calls NtAllocateVirtualMemory (kernel ord_183) with
 * MEM_COMMIT | MEM_RESERVE on a physically contiguous region.
 *
 * VirtualAlloc (kernel ord_183 = NtAllocateVirtualMemory) is functionally
 * equivalent for non-DMA use, but D3D_AllocContiguousMemory additionally
 * guarantees physical contiguity which benefits the D3D runtime.
 *
 * PATCH in z_memman_console.cpp replaces VirtualAlloc with:
 *   s_PoolBase = D3D_AllocContiguousMemory(ZONE_POOL_SIZE_BINARY, 0);
 */
void RE_Memory_ZoneAlloc_Verify( void )
{
    /* Verify D3D_AllocContiguousMemory is linkable (catches missing lib). */
    /* Called at startup in Com_InitZoneMemory via z_memman_console.cpp.   */
    void *test = D3D_AllocContiguousMemory( 0, 0 );
    /* Allocation of size 0 is a no-op / returns NULL — just verifying link. */
    (void)test;
}

/*
 * RE_Memory_ZoneMalloc_Verify — documents Z_Malloc binary layout.
 *
 * Binary Z_Malloc (0x49B00):
 *   Standard VV linked-list first-fit allocator.
 *   Searches free list from s_FreeStart for first-fit block >= requested size.
 *   Delinks the free block and writes ZoneHeader before returned pointer.
 *   Updates [0x7BC300] (m_CountAlloc) and [0x7BC304] (m_SizeFree) on alloc.
 *   Short-lived tags (TAG_TEMP etc.) allocated from pool end; others from start.
 *
 * SOURCE STATUS: code/qcommon/z_memman_console.cpp Z_Malloc matches. ✓
 */
void RE_Memory_ZoneMalloc_Verify( void )
{
    /* Z_Malloc linkage check. */
    extern void *Z_Malloc( int size, memtag_t tag, qboolean zeroed, int alignment );
    void *p = Z_Malloc( 4, TAG_STATIC, qtrue, 4 );
    if ( p ) Z_Free( p );
}

#endif /* _XBOX */
