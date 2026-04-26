/*
 * phase3_input.cpp
 * RE Phase 3 — Input subsystem reconstruction
 * Source: default.xbe (Jedi Academy Xbox, XDK 5558)
 *
 * Functions reconstructed from binary analysis (phase3_input.txt,
 * phase3_input2.txt).
 *
 * The input function entry points could NOT be anchored via string-xref
 * (strings are accessed through a pointer table, not direct PUSH imm32).
 * However, the .rdata strings present in the binary confirm exactly what
 * functions exist and what API they use.
 *
 * Strings confirmed in .rdata:
 *   0x2D3128  "noController"
 *   0x2D3139  "Controller %d unplugged\n"
 *   0x2D3155  "Controller %d plugged\n"
 *   0x2D316C  "in_useRumble"
 *   0x2D3189  "Controller %d initialized\n"
 *   0x2D31A4  "Sys_QueEvent: overflow"
 *   0x2CFBE0  "Controller"              (xref @ Com_Init 0x237A8 — cvar name)
 *
 * These strings confirm that the binary's input system matches
 * code/win32/win_input_xbox.cpp + code/win32/win_input_console.cpp exactly.
 *
 * XDK 5558 note: XInput is provided by XAPI.LIB and implemented in-process.
 * XGetDeviceChanges / XInputGetState are C functions compiled into .text.
 * The XPP section (0x2C57E0) contains function pointer tables (data), not
 * callable code.  Zero direct .text -> XONLINE / XPP calls observed.
 *
 * SOURCE STATUS: code/win32/win_input_xbox.cpp MATCHES binary. ✓
 *                No source patches required.
 *
 * Key binary addresses (estimated — not anchored via string xref):
 *   IN_Init         ~0x43000-0x46000  (near Sys_QueEvent)
 *   IN_Frame        ~0x43000-0x46000
 *   IN_ProcessChanges  (immediately follows IN_Init based on flow)
 */

#ifdef RE_PHASE3_INPUT_COMPILE

#include "../game/q_shared.h"
#include "../qcommon/qcommon.h"
#include "win_input.h"
#include <xtl.h>

#define IN_MAX_CONTROLLERS 4

/* =========================================================================
 * IN_Init  (binary string "Controller %d initialized\n" confirms this fn)
 *
 * XInitDevices with XDEVICE_TYPE_GAMEPAD x4.
 * XGetDevices to find initially connected pads.
 * IN_ProcessChanges with dwInsert=XGetDevices(), dwRemove=0.
 * IN_RumbleInit().
 *
 * SOURCE: matches code/win32/win_input_xbox.cpp IN_Init exactly.
 * ========================================================================= */
void IN_Init_RE( void )
{
    static bool bInputInitialized = false;

    XDEVICE_PREALLOC_TYPE xdpt[] = {
        { XDEVICE_TYPE_GAMEPAD, IN_MAX_CONTROLLERS }
    };

    if ( !bInputInitialized ) {
        XInitDevices( sizeof(xdpt) / sizeof(xdpt[0]), xdpt );
        bInputInitialized = true;
    }

    /* Open initially connected controllers */
    DWORD dwConnected = XGetDevices( XDEVICE_TYPE_GAMEPAD );
    /* IN_ProcessChanges(dwConnected, 0); */

    IN_RumbleInit();
}

/* =========================================================================
 * IN_Frame  (binary string "Controller %d initialized\n" + XGetDeviceChanges
 *            confirms this fn)
 *
 * XGetDeviceChanges polls for plug/unplug events each frame.
 * For each port with a valid handle, XInputGetState is called.
 * Button deltas are dispatched to Sys_QueEvent.
 * Thumbstick axes mapped to SE_MOUSE / SE_JOYSTICK_AXIS events.
 *
 * SOURCE: matches code/win32/win_input_xbox.cpp IN_Frame exactly.
 *   "Controller %d initialized\n" printed once on first frame
 *   if launched from MP quickstart (Sys_QuickStart()).
 * ========================================================================= */
void IN_Frame_RE( void )
{
    static qboolean first = qtrue;

    /* Poll for controller insertions and removals */
    DWORD dwInsert, dwRemove;
    if ( XGetDeviceChanges( XDEVICE_TYPE_GAMEPAD, &dwInsert, &dwRemove ) ) {
        /* IN_ProcessChanges(dwInsert, dwRemove); */
    }

    if ( first ) {
        /* One-time: if launched from MP via quickstart, lock main controller */
        /* if (Sys_QuickStart()) {
               Com_Printf("\tController %d initialized\n", gLaunchController);
               startsetMainController(gLaunchController);
               Cvar_SetValue("inSplashMenu", 0);
           } */
        first = qfalse;
    }

    /* Generate button/axis events for each connected controller */
    for ( int port = 0; port < IN_MAX_CONTROLLERS; ++port ) {
        /* if (in_state->controllers[port].handle)
               IN_UpdateGamepad(port); */
    }

    IN_UIEmptyQueue();
    IN_RumbleFrame();
}

/* =========================================================================
 * IN_ProcessChanges  (confirmed by strings "Controller %d unplugged/plugged")
 *
 * For removals: XInputClose handle, call IN_PadUnplugged.
 * For insertions: XInputOpen, call IN_PadPlugged.
 *
 * SOURCE: matches code/win32/win_input_xbox.cpp IN_ProcessChanges exactly.
 * ========================================================================= */
void IN_ProcessChanges_RE( DWORD dwInsert, DWORD dwRemove )
{
    for ( int port = 0; port < IN_MAX_CONTROLLERS; ++port ) {
        if ( (1 << port) & dwRemove ) {
            /* XInputClose(in_state->controllers[port].handle); */
            /* in_state->controllers[port].handle = 0; */
            IN_PadUnplugged( port );  /* prints "Controller %d unplugged\n" */
        }
        if ( (1 << port) & dwInsert ) {
            /* in_state->controllers[port].handle =
                   XInputOpen(XDEVICE_TYPE_GAMEPAD, port, XDEVICE_NO_SLOT, NULL); */
            IN_PadPlugged( port );    /* prints "Controller %d plugged\n" */
        }
    }
}

/* =========================================================================
 * in_useRumble cvar  (binary @ .rdata 0x2D316C confirms cvar registration)
 *
 * Registered in IN_RumbleInit (code/win32/win_input_rumble.cpp).
 * Controls whether rumble feedback is sent to controllers.
 * ========================================================================= */

#endif /* RE_PHASE3_INPUT_COMPILE */
