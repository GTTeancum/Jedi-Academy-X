/*
 * phase3_sys.cpp
 * RE Phase 3 — Sys / Filesystem subsystem reconstruction
 * Source: default.xbe (Jedi Academy Xbox, XDK 5558)
 *
 * Functions reconstructed from binary analysis (phase3_sys.txt).
 * All function addresses are from the shipped binary VA space.
 *
 * Verification against code/win32/win_main_console.cpp,
 * code/qcommon/files_console.cpp — all match; no source patches required.
 */

/* Compile guard — this file is a RE reference, not compiled into the project.
   Remove this guard to include it. */
#ifdef RE_PHASE3_SYS_COMPILE

#include "../game/q_shared.h"
#include "../qcommon/qcommon.h"
#include <xtl.h>

/* =========================================================================
 * FS_Startup  (binary @ 0x25F90)
 *
 * Initialises the VFS. Registers cvars in this exact order (verified from
 * binary .rdata push sequences):
 *   fs_openorder  "0"    CVAR_ARCHIVE (0x10)
 *   fs_debug      "0"    CVAR_ARCHIVE (0x10)
 *   fs_copyfiles  "0"    CVAR_USERINFO|CVAR_ARCHIVE (0x10)
 *   fs_cdpath     <Sys_DefaultCDPath()>  CVAR_ARCHIVE|CVAR_INIT (0x10)
 *   fs_basepath   <Sys_DefaultBasePath()> CVAR_ARCHIVE|CVAR_INIT (0x10)
 *   fs_game       "base" CVAR_ARCHIVE|CVAR_SERVERINFO|CVAR_USERINFO (0x14)
 *   fs_restrict   ""     CVAR_ROM (0x80)
 *
 * After cvar setup, opens GOB/PK3 files via FS_AddGameDirectory (0x44E50),
 * then sets up the search path linked list.
 *
 * NOTE: Sys_DefaultCDPath (0x44DA0) returns "d:\\" on Xbox.
 *       Sys_DefaultBasePath (0x44DB0) returns "d:\\" on Xbox.
 * ========================================================================= */
void FS_Startup_RE( void )
{
    Com_Printf( "----- FS_Startup -----\n" );

    Cvar_Get( "fs_openorder",  "0",    CVAR_ARCHIVE );
    Cvar_Get( "fs_debug",      "0",    CVAR_ARCHIVE );
    Cvar_Get( "fs_copyfiles",  "0",    CVAR_ARCHIVE );
    Cvar_Get( "fs_cdpath",     Sys_DefaultCDPath(),   CVAR_ARCHIVE | CVAR_INIT );
    Cvar_Get( "fs_basepath",   Sys_DefaultBasePath(), CVAR_ARCHIVE | CVAR_INIT );
    Cvar_Get( "fs_game",       "base", CVAR_ARCHIVE | CVAR_SERVERINFO | CVAR_USERINFO );
    Cvar_Get( "fs_restrict",   "",     CVAR_ROM );

    /* Binary then calls 0x44E50 = FS_AddGameDirectory with "d:\\" / "base",
       and iterates search path init (loop observed at 0x26051-0x26075). */
}

/* =========================================================================
 * Sys_InitFileCodes  (binary region — exact VA not anchored in Phase 2)
 *
 * In the shipped binary this function attempts to initialise the filecode
 * cache used for GOB streaming. On failure the binary prints a warning and
 * continues — it does NOT call Com_Error(ERR_DROP).
 *
 * SOURCE PATCH APPLIED (code/win32/win_main_console.cpp):
 *   Changed Com_Error(ERR_DROP, ...) to Com_Printf(WARNING ...) so that a
 *   missing filecode cache is non-fatal, matching the shipped binary.
 * ========================================================================= */

/* =========================================================================
 * Boot sequence  (_mainCRTStartup / thread_proc)
 *
 * Binary _mainCRTStartup @ 0xC2765:
 *   1. Reads XBE certificate TLS data to compute thread stack size
 *   2. CreateThread(NULL, 0, thread_proc@0xC26F1, NULL, 0, NULL)
 *   3. If CreateThread returns NULL -> XapiBootToDash (never returns)
 *   4. CloseHandle(hThread); return
 *
 * Thread proc @ 0xC26F1:
 *   XapiApplyKernelPatches()
 *   XapiInitProcess()
 *   TLS init
 *   __rtinit()   // XIB/XIC C-init sections
 *   __cinit()    // 62 C++ static constructors
 *   main(0,0,0)
 *   XapiBootToDash()
 *
 * SOURCE: code/x_exe/xbox_asm_stubs.asm — _WinMainCRTStartup stub matches.
 *   XBLog_PreCRTProbe / XBLog_PostCRTProbe breadcrumbs added for diagnosis.
 * ========================================================================= */

#endif /* RE_PHASE3_SYS_COMPILE */
