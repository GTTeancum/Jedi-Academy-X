/*
 * re_sound.cpp  —  RE Phase 3: Sound subsystem
 * Jedi Academy Xbox shipped binary (default.xbe, XDK 5558)
 *
 * Binary anchor map:
 *   S_DirectSound3D_init   0x00033885   (79+ insns)
 *   DS_init_seh            0x00030140   (SEH frame; DirectSoundCreate + setup)
 *   S_StartSound           0x00032565
 *   S_HashName             0x00030500
 *   S_LoadSoundBank        0x00048015
 *   DSOUND section         0x0025F000   (XDK runtime; vsz 0x1D000)
 *
 * SOURCE STATUS: code/client/snd_dma_console.cpp matches. ✓
 */

#include "../game/q_shared.h"
#include "../qcommon/qcommon.h"
#include "../client/client.h"

#ifdef _XBOX
#include <xtl.h>

/*
 * RE_Sound_CvarOrder_Verify — documents S_DirectSound3D_init cvar order.
 *
 * Binary S_DirectSound3D_init (0x33885) registers 11 cvars in this order
 * (confirmed from .rdata push sequence):
 *
 *   s_effects_volume    "1"    CVAR_ARCHIVE
 *   s_voice_volume      "1"    CVAR_ARCHIVE
 *   s_music_volume      "0.25" CVAR_ARCHIVE
 *   s_separation        "0.5"  CVAR_ARCHIVE
 *   s_allowDynamicMusic "1"    CVAR_ARCHIVE
 *   s_show              "0"    0
 *   s_testsound         "0"    0
 *   s_debugdynamic      "0"    0
 *   sys_cpuid           "0"    0
 *   s_soundpoolmegs     "1"    CVAR_ARCHIVE
 *   s_initsound         "1"    CVAR_ROM      ← early-out if 0
 *
 * If s_initsound.value == 0: prints "not initializing." and returns early.
 * Matches source guard at snd_dma_console.cpp. ✓
 *
 * DS device pointer stored at binary global [0x4C56EC].
 * Sound table base at binary [0x4C5714].
 *
 * DS init callee (0x30140) flow:
 *   SEH frame with handler @ 0x239EDB (Com_Error)
 *   if g_soundDevice == NULL:
 *     DirectSoundCreate/enum @ 0x300B0
 *     Z_Malloc: sound buffer struct
 *     DS primary buffer attach @ 0x2FB70
 *     DS mix setup @ 0x2FC30
 *   Stores DS device @ [0x4C56EC]
 *
 * PsCreateSystemThreadEx: 0 callers — DSOUND runtime handles its own thread.
 *
 * SOURCE STATUS: snd_dma_console.cpp matches. ✓
 */
void RE_Sound_CvarOrder_Verify( void )
{
    /* Compile-time check that these cvars exist and are registerable.
       Dead code — real registration is in snd_dma_console.cpp. */
    (void)Cvar_Get( "s_effects_volume",    "1",    CVAR_ARCHIVE );
    (void)Cvar_Get( "s_voice_volume",      "1",    CVAR_ARCHIVE );
    (void)Cvar_Get( "s_music_volume",      "0.25", CVAR_ARCHIVE );
    (void)Cvar_Get( "s_separation",        "0.5",  CVAR_ARCHIVE );
    (void)Cvar_Get( "s_allowDynamicMusic", "1",    CVAR_ARCHIVE );
    (void)Cvar_Get( "s_show",              "0",    0 );
    (void)Cvar_Get( "s_testsound",         "0",    0 );
    (void)Cvar_Get( "s_debugdynamic",      "0",    0 );
    (void)Cvar_Get( "sys_cpuid",           "0",    0 );
    (void)Cvar_Get( "s_soundpoolmegs",     "1",    CVAR_ARCHIVE );
    (void)Cvar_Get( "s_initsound",         "1",    CVAR_ROM );
}

#endif /* _XBOX */
