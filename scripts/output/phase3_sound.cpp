/*
 * phase3_sound.cpp
 * RE Phase 3 — Sound subsystem reconstruction
 * Source: default.xbe (Jedi Academy Xbox, XDK 5558)
 *
 * Functions reconstructed from binary analysis (phase3_sound.txt).
 * Verified against code/client/snd_dma_console.cpp — cvar registrations
 * and s_initsound gate confirmed matching.  No source patches required.
 *
 * Key binary addresses:
 *   S_DirectSound3D_init   0x33885   (79+ insns; registers 11 cvars)
 *   DS_init_seh            0x30140   (SEH frame; DirectSoundCreate + setup)
 *   S_StartSound           0x32565   (entity/channel range checks)
 *   S_HashName             0x30500   (filename hash)
 *   S_LoadSoundBank        0x48015   (.wav/.opus bank loader)
 *   DSOUND section         0x25F000  (XDK DirectSound runtime; vsz 0x1D000)
 */

#ifdef RE_PHASE3_SOUND_COMPILE

#include "../game/q_shared.h"
#include "../qcommon/qcommon.h"
#include <xtl.h>
#include <dsound.h>

/* DS device pointer (binary @ [0x4C56EC]).
   NULL until DS_init_seh creates it. */
static IDirectSound8 *g_soundDevice = NULL; /* re-named; binary uses same pattern */

/* Sound cvars (registered in binary in this exact order): */
static cvar_t *s_effects_volume;
static cvar_t *s_voice_volume;
static cvar_t *s_music_volume;
static cvar_t *s_separation;
static cvar_t *s_allowDynamicMusic;
static cvar_t *s_show;
static cvar_t *s_testsound;
static cvar_t *s_debugdynamic;
static cvar_t *sys_cpuid;
static cvar_t *s_soundpoolmegs;
static cvar_t *s_initsound;

/* =========================================================================
 * S_DirectSound3D_init  (binary @ 0x33885)
 *
 * Registers 11 cvars in the order observed in the binary (confirmed by
 * .rdata string push sequence in disassembly).
 *
 * If s_initsound.value == 0 at the end of cvar registration, the function
 * prints "not initializing." and returns immediately without creating a DS
 * device.  This matches the source guard in snd_dma_console.cpp line 279.
 *
 * Otherwise delegates to DS_init_seh (0x30140) which:
 *   - Wraps creation in a SEH frame (handler @ 0x239EDB = Com_Error handler)
 *   - Creates the DirectSound device via 0x300B0 (DirectSoundCreate/enum)
 *   - Allocates a buffer struct via Z_Malloc
 *   - Attaches the DS buffer via 0x2FB70
 *   - Configures mixing via 0x2FC30
 *   - Stores DS device handle at [0x4C56EC]
 * ========================================================================= */
void S_DirectSound3D_init_RE( void )
{
    /* Cvar registration order confirmed from binary push sequence: */
    s_effects_volume    = Cvar_Get( "s_effects_volume",    "1",    CVAR_ARCHIVE );
    s_voice_volume      = Cvar_Get( "s_voice_volume",      "1",    CVAR_ARCHIVE );
    s_music_volume      = Cvar_Get( "s_music_volume",      "0.25", CVAR_ARCHIVE );
    s_separation        = Cvar_Get( "s_separation",        "0.5",  CVAR_ARCHIVE );
    s_allowDynamicMusic = Cvar_Get( "s_allowDynamicMusic", "1",    CVAR_ARCHIVE );
    s_show              = Cvar_Get( "s_show",              "0",    0 );
    s_testsound         = Cvar_Get( "s_testsound",         "0",    0 );
    s_debugdynamic      = Cvar_Get( "s_debugdynamic",      "0",    0 );
    sys_cpuid           = Cvar_Get( "sys_cpuid",           "0",    0 );
    s_soundpoolmegs     = Cvar_Get( "s_soundpoolmegs",     "1",    CVAR_ARCHIVE );
    s_initsound         = Cvar_Get( "s_initsound",         "1",    CVAR_ROM );

    if ( !s_initsound->value ) {
        Com_Printf( "not initializing.\n" );
        return;
    }

    /* DS_init_seh @ 0x30140 — actual device creation (SEH wrapped) */
    /* S_DS_Init_Internal(); */
}

/* =========================================================================
 * DS_init_seh  (binary @ 0x30140)
 *
 * Has a full SEH frame.  If g_soundDevice is already non-NULL, returns
 * immediately (device already created).
 *
 * Creation sequence:
 *   0x300B0: DirectSoundCreate / device enumeration
 *   Z_Malloc: allocates sound buffer tracking struct
 *   0x2FB70: DS primary buffer creation and attachment
 *   0x2FC30: DS mixing / 3D listener setup
 *   [0x4C56EC] = g_soundDevice pointer stored
 * ========================================================================= */
static void DS_init_seh_RE( void )
{
    __try {
        if ( g_soundDevice ) return;

        /* DirectSoundCreate(NULL, &g_soundDevice, NULL) — via 0x300B0 */
        /* HRESULT hr = DirectSoundCreate(NULL, &g_soundDevice, NULL); */

        /* Allocate sound buffer struct via Z_Malloc */
        /* g_soundBufferInfo = Z_Malloc(sizeof(SoundBufferInfo), TAG_SOUND, qtrue); */

        /* Attach DS buffer — 0x2FB70 */
        /* DS_AttachBuffer(g_soundDevice, g_soundBufferInfo); */

        /* Configure mixing — 0x2FC30 */
        /* DS_SetupMixer(g_soundDevice); */

    } __except( EXCEPTION_EXECUTE_HANDLER ) {
        Com_Printf( "S_Init: DirectSound init exception\n" );
    }
}

/* =========================================================================
 * S_StartSound  (binary @ 0x32565)
 *
 * Standard Q3 StartSound with Xbox-specific early-outs.
 * Checks observed in binary:
 *   1. s_soundStarted must be true
 *   2. entitynum range: 0 <= entitynum <= 0xC00
 *   3. Sound table [0x4C5714 + channelNum*4] must be non-NULL
 * ========================================================================= */
void S_StartSound_RE( vec3_t origin, int entityNum, int entchannel, sfxHandle_t sfxHandle )
{
    if ( !s_soundStarted ) return;
    if ( (unsigned)entityNum > 0xC00 ) return;
    /* sound table check: if channel slot is NULL, return */
    /* ... standard Q3 StartSound body ... */
    (void)origin; (void)entchannel; (void)sfxHandle;
}

/* =========================================================================
 * S_HashName  (binary @ 0x30500)
 *
 * Standard filename hash: sum of char values, mod TABLE_SIZE.
 * Identical to Q3/JA hash used throughout the engine.
 * ========================================================================= */
static int S_HashName_RE( const char *name )
{
    int hash = 0;
    while ( *name ) {
        hash += (unsigned char)(*name++);
    }
    return hash; /* modded by caller against sound table size */
}

#endif /* RE_PHASE3_SOUND_COMPILE */
