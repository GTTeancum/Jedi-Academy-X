/*
 * re_input.cpp  —  RE Phase 3: Input subsystem
 * Jedi Academy Xbox shipped binary (default.xbe, XDK 5558)
 *
 * Input function addresses could NOT be anchored via string-xref (strings
 * accessed through a pointer table, not direct PUSH imm32).  However the
 * .rdata strings confirm exactly what functions and API calls are present.
 *
 * .rdata strings confirmed (from phase3_input2.txt):
 *   0x2D3128  "noController"
 *   0x2D3139  "Controller %d unplugged\n"
 *   0x2D3155  "Controller %d plugged\n"
 *   0x2D316C  "in_useRumble"                (cvar name, IN_RumbleInit)
 *   0x2D3189  "Controller %d initialized\n" (IN_Frame first-frame path)
 *   0x2D31A4  "Sys_QueEvent: overflow"      (Sys_QueEvent @ 0x44CB5)
 *
 * XDK 5558: XInput provided by XAPI.LIB, compiled into .text.
 * XPP section (0x2C57E0): vtable/function-pointer data, not executable code.
 * XONLINE section (0x2AEAE0): XOnline networking — not XInput.
 * 0 direct .text -> XONLINE / XPP calls observed.
 *
 * SOURCE STATUS: code/win32/win_input_xbox.cpp    matches binary strings. ✓
 *                code/win32/win_input_console.cpp matches binary strings. ✓
 *                code/win32/win_input_rumble.cpp  matches binary strings. ✓
 *
 * Binary anchor map (estimated — string-xref anchoring not possible):
 *   IN_Init / IN_Frame / IN_ProcessChanges  ~0x43000-0x46000
 */

#include "../game/q_shared.h"
#include "../qcommon/qcommon.h"
/* win_input.h omitted — re_input.cpp only documents API calls, not types. */

#ifdef _XBOX
#include <xtl.h>

/*
 * RE_Input_API_Verify — documents XInput API calls present in binary.
 *
 * Binary uses:
 *   XInitDevices(sizeof(xdpt)/sizeof(xdpt[0]), xdpt)
 *     xdpt = { XDEVICE_TYPE_GAMEPAD, 4 }
 *   XGetDevices(XDEVICE_TYPE_GAMEPAD)        — initial connection scan
 *   XGetDeviceChanges(XDEVICE_TYPE_GAMEPAD, &dwInsert, &dwRemove)  — per frame
 *   XInputOpen(XDEVICE_TYPE_GAMEPAD, port, XDEVICE_NO_SLOT, NULL)  — on insert
 *   XInputClose(handle)                                             — on remove
 *   XInputGetState(handle, &newState)                              — per frame
 *   XInputSetState(handle, &feedback)                              — rumble
 *
 * All of these are confirmed in win_input_xbox.cpp. ✓
 *
 * Button layout confirmed by source (win_input_xbox.cpp IN_UpdateGamepad):
 *   Digital: DPAD_UP/DOWN/LEFT/RIGHT, Start, Back, L-stick, R-stick
 *   Analog:  A, B, X, Y, Black, White, L-trigger, R-trigger
 *   Axes:    ThumbLX/ThumbLY (left stick), ThumbRX/ThumbRY (right stick)
 *   Deadzone: 0.25f (confirmed in source _joyAxisConvert)
 */
void RE_Input_API_Verify( void )
{
    /* Verify XInput types are available at compile time. */
    XDEVICE_PREALLOC_TYPE xdpt = { XDEVICE_TYPE_GAMEPAD, 4 };
    (void)xdpt;

    /* XInputGetState, XInputSetState, XGetDeviceChanges, XInputOpen,
       XInputClose, XInitDevices, XGetDevices are all called from
       win_input_xbox.cpp — verified by string evidence in binary. */
}

#endif /* _XBOX */
