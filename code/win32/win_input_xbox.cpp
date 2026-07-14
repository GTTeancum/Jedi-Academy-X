// win_input.c -- win32 mouse and joystick code
// 02/21/97 JCB Added extended DirectInput code to support external controllers.

// leave this as first line for PCH reasons...
//
// #include "../server/exe_headers.h"

#include <xtl.h>

#include "../client/client.h"

#include "../qcommon/qcommon.h"
#ifdef _JK2MP
#include "../ui/keycodes.h"
#else
#include "../client/keycodes.h"
#endif

#include "win_local.h"
#include "win_input.h"
#include "xb_log.h"

#define IN_MAX_CONTROLLERS 4

/* Plan-B (OpenJKDF2 1:1): set TRUE by main() after it calls XInitDevices
 * BEFORE D3D init (per OpenJKDF2's NV2A USB-host-controller ordering
 * requirement).  IN_Init checks this and skips the redundant call. */
bool g_XInitDevicesAlreadyCalled = false;

void IN_UIEmptyQueue();
void IN_CheckForNoControllers();

struct inputstate_t
{
	struct controller_t
	{
		HANDLE handle;
		XINPUT_STATE state;
		XINPUT_FEEDBACK feedback;
	};
	controller_t controllers[IN_MAX_CONTROLLERS];
};

inputstate_t *in_state = NULL;
static cvar_t *joy_deadzone = NULL;



/*
=========================================================================

JOYSTICK

=========================================================================
*/
//JLF moved here for multiple access (then not used multiple times. oh well.)
extern bool noControllersConnected;
// Process all the insertions and removals, updating handles and such
void IN_ProcessChanges(DWORD dwInsert, DWORD dwRemove)
{
	if (dwInsert || dwRemove)
	{
		XBLF("STEFX: IN_ProcessChanges insert=0x%08x remove=0x%08x\n", dwInsert, dwRemove);
	}

	for(int port = 0; port < IN_MAX_CONTROLLERS; ++port)
	{
		// Close removals.
		if((1 << port) & dwRemove)
		{
			if ( in_state->controllers[port].handle )
			{
				XInputClose( in_state->controllers[port].handle );
				in_state->controllers[port].handle = 0;
			}
#if defined(STEFX_ELITE_FORCE_SP)
			CL_STEFX_SplitScreen_RecordPadState( port, qfalse, IN_GetMainController(), 0, NULL, 0, 0, 0, 0 );
#endif
			IN_PadUnplugged(port);

		}

		// Open insertions.
		if( (1 << port) & dwInsert )
		{
			in_state->controllers[port].handle = XInputOpen( XDEVICE_TYPE_GAMEPAD, port, XDEVICE_NO_SLOT, NULL );
			XBLF("STEFX: controller %d open handle=%p\n", port, in_state->controllers[port].handle);
			IN_PadPlugged(port);
		}
	}

	return;
}

/*********
IN_CheckForNoControllers()
If there are no controllers plugged in, the UI
is notified so it can display an appropriate
message.
*********/
void IN_CheckForNoControllers()
{
	extern bool noControllersConnected;
	if(!noControllersConnected)
	{
		extern bool wasPlugged[4];
		if( !wasPlugged[0] &&
			!wasPlugged[1] &&
			!wasPlugged[2] &&
			!wasPlugged[3] )
		{
			// Tell the UI that there are no controllers connected
		//	VM_Call( uivm, UI_CONTROLLER_UNPLUGGED, true, -1);
			noControllersConnected = true;
		}
	}
}

/*
=========================================================================

  RUMBLE SUPPORT

=========================================================================
*/

bool IN_RumbleAdjust(int controller, int left, int right)
{
	assert(controller >= 0 && controller < IN_MAX_CONTROLLERS);

	// Get a device handle for the controller.  This may fail.
	HANDLE handle = in_state->controllers[controller].handle;

	if (!handle) return false;
	
	XINPUT_FEEDBACK* fb = &in_state->controllers[controller].feedback;
	
	// If a prior rumble update is still pending, go away
	if (fb->Header.dwStatus == ERROR_IO_PENDING) return false;

	fb->Rumble.wLeftMotorSpeed = left;
	fb->Rumble.wRightMotorSpeed = right;
	
	return ERROR_IO_PENDING == XInputSetState(handle, fb);
}


/*
=========================================================================

=========================================================================
*/

/*
igBool IN_WindowClose(igWindow *window)
{
	SV_Shutdown ("Server quit\n");
	CL_Shutdown ();
	Com_Shutdown ();
	Sys_Quit ();
	return true;
}
*/

/*
===========
IN_Shutdown
===========
*/
void IN_Shutdown( void ) {
	IN_RumbleShutdown();

	delete in_state;
	in_state = NULL;
}


/*
===========
IN_Init
===========
*/
void IN_Init( void )
{
		in_state = new inputstate_t;

		// Initialize support for 4 gamepads
		XDEVICE_PREALLOC_TYPE xdpt[] = {
			{XDEVICE_TYPE_GAMEPAD, 4}
		};

    // Initialize the peripherals. We can only ever
	// call XInitDevices once, no matter what.
	// Plan-B: main() now calls XInitDevices early (before D3D init,
	// per OpenJKDF2's NV2A ordering requirement) and sets the global
	// g_XInitDevicesAlreadyCalled.  This block re-uses that flag.
	extern bool g_XInitDevicesAlreadyCalled;
	if (!g_XInitDevicesAlreadyCalled) {
		XInitDevices( sizeof(xdpt) / sizeof(XDEVICE_PREALLOC_TYPE), xdpt );
		g_XInitDevicesAlreadyCalled = true;
	}

		// Zero all of our data, including handles
		memset(in_state->controllers, 0, sizeof(in_state->controllers));

		// Find out the status of all gamepad ports, then open them
		DWORD deviceMask = XGetDevices( XDEVICE_TYPE_GAMEPAD );
		XBLF("STEFX: IN_Init gamepad mask=0x%08x\n", deviceMask);
		IN_ProcessChanges( deviceMask, 0 );

		joy_deadzone = Cvar_Get( "joy_deadzone", "0.18", CVAR_ARCHIVE );

		IN_RumbleInit();
	}

static inline float _joyAxisConvert(SHORT x)
{
	// Change scale
	float y = x / 32767.0;
	float deadzone = joy_deadzone ? joy_deadzone->value : 0.18f;

	if (deadzone < 0.0f)
	{
		deadzone = 0.0f;
	}
	else if (deadzone > 0.95f)
	{
		deadzone = 0.95f;
	}

	if(fabs(y) < deadzone)
	{
		y = 0.0f;
	}

	return y;
}

// How many controls on the xbox gamepad?
#define IN_NUM_DIGITAL_BUTTONS 8
#define IN_NUM_ANALOG_BUTTONS 8
// Cutoff where the analog buttons are considered to be "pressed"
// This should be smarter.
#define IN_ANALOG_BUTTON_THRESHOLD 64

#if defined(STEFX_ELITE_FORCE_SP)
static WORD s_stefxLastMenuButtons[IN_MAX_CONTROLLERS] = { 0, 0, 0, 0 };
static bool s_stefxMenuButtonsPrimed[IN_MAX_CONTROLLERS] = { false, false, false, false };

static void IN_STEFX_UpdateMenuButtonEdge(int port, WORD changed, WORD current, WORD mask, fakeAscii_t button, const char *name)
{
	if (changed & mask)
	{
		const bool pressed = (current & mask) != 0;
		XBLF("STEFX_INPUT_MENU_RAW_DISPATCH port=%d name='%s' mask=0x%04x fakeAscii=%d pressed=%d main=%d state=%d catcher=0x%x",
			port,
			name,
			(unsigned int)mask,
			(int)button,
			pressed ? 1 : 0,
			IN_GetMainController(),
			(int)cls.state,
			(unsigned int)cls.keyCatchers);
		XBLF("STEFX_MENU_INPUT raw port=%d name='%s' fakeAscii=%d pressed=%d buttons=0x%04x state=%d catcher=0x%x",
			port,
			name,
			(int)button,
			pressed ? 1 : 0,
			(unsigned int)current,
			(int)cls.state,
			(unsigned int)cls.keyCatchers);
		IN_CommonJoyPress(port, button, pressed);
	}
}

static void IN_STEFX_UpdateMenuButtons(int port, WORD buttons)
{
	const WORD menuMask = XINPUT_GAMEPAD_START | XINPUT_GAMEPAD_BACK;
	const WORD current = buttons & menuMask;
	WORD changed;

	if (!s_stefxMenuButtonsPrimed[port])
	{
		s_stefxLastMenuButtons[port] = current;
		s_stefxMenuButtonsPrimed[port] = true;
		XBLF("STEFX_INPUT_MENU_PRIME port=%d buttons=0x%04x main=%d state=%d catcher=0x%x",
			port,
			(unsigned int)current,
			IN_GetMainController(),
			(int)cls.state,
			(unsigned int)cls.keyCatchers);
		return;
	}

	changed = (WORD)(current ^ s_stefxLastMenuButtons[port]);
	if (!changed)
	{
		return;
	}

	XBLF("STEFX_INPUT_MENU_RAW port=%d buttons=0x%04x last=0x%04x changed=0x%04x main=%d state=%d catcher=0x%x",
		port,
		(unsigned int)current,
		(unsigned int)s_stefxLastMenuButtons[port],
		(unsigned int)changed,
		IN_GetMainController(),
		(int)cls.state,
		(unsigned int)cls.keyCatchers);

	s_stefxLastMenuButtons[port] = current;
	IN_STEFX_UpdateMenuButtonEdge(port, changed, current, XINPUT_GAMEPAD_START, A_JOY4, "START");
	IN_STEFX_UpdateMenuButtonEdge(port, changed, current, XINPUT_GAMEPAD_BACK, A_JOY1, "BACK");
}
#endif

void IN_UpdateGamepad(int port)
{
	static bool loggedFirstState[IN_MAX_CONTROLLERS] = { false, false, false, false };
#if defined(STEFX_ELITE_FORCE_SP)
	static int activeStateLogBudget[IN_MAX_CONTROLLERS] = { 16, 16, 16, 16 };
	static int splitSecondaryLogBudget[IN_MAX_CONTROLLERS] = { 16, 16, 16, 16 };
#endif
	// Lookup table to convert the digital buttons to fakeAscii_t, in mask order
	const fakeAscii_t digitalXlat[IN_NUM_DIGITAL_BUTTONS] = {
		A_JOY5, // DPAD_UP
		A_JOY7, // DPAD_DOWN
		A_JOY8, // DPAD_LEFT
		A_JOY6, // DPAD_RIGHT
		A_JOY4, // Start
		A_JOY1, // Back
		A_JOY2, // Left stick
		A_JOY3  // Right stick
	};

	// Lookup table to convet the analog buttons to fakeAscii_t, in DX order
	const fakeAscii_t analogXlat[IN_NUM_ANALOG_BUTTONS] = {
		A_JOY15, // A
		A_JOY14, // B
		A_JOY16, // X
		A_JOY13, // Y
		A_JOY10, // Black
		A_JOY9,  // White
		A_JOY11, // Left trigger
		A_JOY12  // Right trigger
	};

	// Get new state
	XINPUT_STATE newState;
	XInputGetState( in_state->controllers[port].handle, &newState );
#if defined(STEFX_ELITE_FORCE_SP)
	CL_STEFX_SplitScreen_RecordPadState( port, qtrue, IN_GetMainController(), newState.Gamepad.wButtons, newState.Gamepad.bAnalogButtons,
		newState.Gamepad.sThumbLX, newState.Gamepad.sThumbLY, newState.Gamepad.sThumbRX, newState.Gamepad.sThumbRY );
	if (Cvar_VariableIntegerValue("stefx_splitScreen"))
	{
		static int s_splitPrimaryClaimLogBudget = 16;
		const int splitPrimary = CL_STEFX_SplitScreen_PrimaryPadForMainController();
		if (splitPrimary >= 0 && splitPrimary != IN_GetMainController())
		{
			if (s_splitPrimaryClaimLogBudget > 0)
			{
				XBLF("STEFX_SPLIT_INPUT main controller reassigned old=%d new=%d activePort=%d state=%d catcher=0x%x",
					IN_GetMainController(),
					splitPrimary,
					port,
					(int)cls.state,
					(unsigned int)cls.keyCatchers);
				--s_splitPrimaryClaimLogBudget;
			}
			IN_SetMainController(splitPrimary);
		}
	}
#endif
	if (!loggedFirstState[port])
	{
		XBLF("STEFX: first gamepad state port=%d buttons=0x%04x A=%u B=%u X=%u Y=%u LT=%u RT=%u LX=%d LY=%d RX=%d RY=%d\n",
			port,
			newState.Gamepad.wButtons,
			newState.Gamepad.bAnalogButtons[0],
			newState.Gamepad.bAnalogButtons[1],
			newState.Gamepad.bAnalogButtons[2],
			newState.Gamepad.bAnalogButtons[3],
			newState.Gamepad.bAnalogButtons[6],
			newState.Gamepad.bAnalogButtons[7],
			newState.Gamepad.sThumbLX,
			newState.Gamepad.sThumbLY,
			newState.Gamepad.sThumbRX,
			newState.Gamepad.sThumbRY);
		loggedFirstState[port] = true;
	}
#if defined(STEFX_ELITE_FORCE_SP)
	if (activeStateLogBudget[port] > 0 &&
		(newState.Gamepad.wButtons ||
		 newState.Gamepad.bAnalogButtons[0] ||
		 newState.Gamepad.bAnalogButtons[1] ||
		 newState.Gamepad.bAnalogButtons[2] ||
		 newState.Gamepad.bAnalogButtons[3] ||
		 newState.Gamepad.bAnalogButtons[6] ||
		 newState.Gamepad.bAnalogButtons[7] ||
		 newState.Gamepad.sThumbLX ||
		 newState.Gamepad.sThumbLY ||
		 newState.Gamepad.sThumbRX ||
		 newState.Gamepad.sThumbRY))
	{
		XBLF("STEFX: active gamepad port=%d buttons=0x%04x A=%u B=%u X=%u Y=%u LT=%u RT=%u LX=%d LY=%d RX=%d RY=%d",
			port,
			newState.Gamepad.wButtons,
			newState.Gamepad.bAnalogButtons[0],
			newState.Gamepad.bAnalogButtons[1],
			newState.Gamepad.bAnalogButtons[2],
			newState.Gamepad.bAnalogButtons[3],
			newState.Gamepad.bAnalogButtons[6],
			newState.Gamepad.bAnalogButtons[7],
			newState.Gamepad.sThumbLX,
			newState.Gamepad.sThumbLY,
			newState.Gamepad.sThumbRX,
			newState.Gamepad.sThumbRY);
		activeStateLogBudget[port]--;
	}
#endif

#if defined(STEFX_ELITE_FORCE_SP)
	if (Cvar_VariableIntegerValue("stefx_splitScreen") && CL_STEFX_SplitScreen_ShouldReservePadForP2(port, IN_GetMainController()))
	{
		IN_STEFX_UpdateMenuButtons(port, newState.Gamepad.wButtons);
		if (splitSecondaryLogBudget[port] > 0)
		{
			XBLF("STEFX_SPLIT_INPUT secondary pad reserved for P2 port=%d rawMain=%d buttons=0x%04x A=%u B=%u X=%u Y=%u LT=%u RT=%u LX=%d LY=%d RX=%d RY=%d state=%d catcher=0x%x",
				port,
				IN_GetMainController(),
				newState.Gamepad.wButtons,
				newState.Gamepad.bAnalogButtons[0],
				newState.Gamepad.bAnalogButtons[1],
				newState.Gamepad.bAnalogButtons[2],
				newState.Gamepad.bAnalogButtons[3],
				newState.Gamepad.bAnalogButtons[6],
				newState.Gamepad.bAnalogButtons[7],
				newState.Gamepad.sThumbLX,
				newState.Gamepad.sThumbLY,
				newState.Gamepad.sThumbRX,
				newState.Gamepad.sThumbRY,
				(int)cls.state,
				(unsigned int)cls.keyCatchers);
			splitSecondaryLogBudget[port]--;
		}
		in_state->controllers[port].state = newState;
		return;
	}
#endif

	// Get old state
	XINPUT_STATE &oldState(in_state->controllers[port].state);

#if defined(STEFX_ELITE_FORCE_SP)
	IN_STEFX_UpdateMenuButtons(port, newState.Gamepad.wButtons);
#endif

	int buttonIdx;
	bool oldPressed, newPressed;

	// Check all digital buttons first
	for (buttonIdx = 0; buttonIdx < IN_NUM_DIGITAL_BUTTONS; ++buttonIdx)
	{
#if defined(STEFX_ELITE_FORCE_SP)
		if (buttonIdx == 4 || buttonIdx == 5)
		{
			continue;
		}
#endif
		oldPressed = oldState.Gamepad.wButtons & (1 << buttonIdx);
		newPressed = newState.Gamepad.wButtons & (1 << buttonIdx);

		if (oldPressed != newPressed)
		{
#if defined(STEFX_ELITE_FORCE_SP)
			if (buttonIdx == 4 || buttonIdx == 5)
			{
				XBLF("STEFX_INPUT_MENU_DIGITAL_EDGE port=%d buttonIdx=%d fakeAscii=%d pressed=%d state=%d catcher=0x%x",
					port,
					buttonIdx,
					(int)digitalXlat[buttonIdx],
					newPressed ? 1 : 0,
					(int)cls.state,
					(unsigned int)cls.keyCatchers);
			}
#endif
			IN_CommonJoyPress(port, digitalXlat[buttonIdx], newPressed);
		}
	}

	// Now check all analog buttons
	for (buttonIdx = 0; buttonIdx < IN_NUM_ANALOG_BUTTONS; ++buttonIdx)
	{
		oldPressed = oldState.Gamepad.bAnalogButtons[buttonIdx] > IN_ANALOG_BUTTON_THRESHOLD;
		newPressed = newState.Gamepad.bAnalogButtons[buttonIdx] > IN_ANALOG_BUTTON_THRESHOLD;

		if (oldPressed != newPressed)
			IN_CommonJoyPress(port, analogXlat[buttonIdx], newPressed);
	}

	// Update joysticks
	_padInfo.joyInfo[0].x = _joyAxisConvert(newState.Gamepad.sThumbLX);
	_padInfo.joyInfo[0].y = _joyAxisConvert(newState.Gamepad.sThumbLY);
	_padInfo.joyInfo[1].x = _joyAxisConvert(newState.Gamepad.sThumbRX);
	_padInfo.joyInfo[1].y = _joyAxisConvert(newState.Gamepad.sThumbRY);
	_padInfo.joyInfo[0].valid = _padInfo.joyInfo[1].valid = true;
	_padInfo.padId = port;

	// Copy state back
	oldState = newState;

	// Update game
	IN_CommonUpdate();
}

extern qboolean CurrentStateIsInteractive();
extern int mainControllerDelayedUnplug;

extern void startsetMainController(int controller);
extern int gLaunchController;
/*
==================
IN_Frame

Called every frame, even if not generating commands
==================
*/
//extern int ignoreInputTime;
extern vmCvar_t ControllerOutNum;
void IN_Frame (void)
{
	static qboolean first = qtrue;
	static int callCount = 0;
	const qboolean xboxTraceInput = qfalse;
	if (xboxTraceInput) XBLF("JA: IN_TIGHT #%d enter in_state=%p", callCount, (void*)in_state);
	if (callCount < 2) {
		XBLog_Write(va("JA: IN_Frame #%d entered, in_state=%p", callCount, (void*)in_state));
	}
	callCount++;
	if (in_state)
	{
		// First, check for changes in device status (removed/inserted pads)
		DWORD dwInsert, dwRemove;
		if( XGetDeviceChanges( XDEVICE_TYPE_GAMEPAD, &dwInsert, &dwRemove ) )
		{
			IN_ProcessChanges(dwInsert, dwRemove);
		}

		if ( first )
		{
			// We only force the controller to be locked when we came from MP:
			extern bool Sys_QuickStart( void );
			bool quickStart = Sys_QuickStart();
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
			extern bool Sys_IsDirectMapBoot(void);
			if ( Sys_IsDirectMapBoot() )
			{
				if ( quickStart )
				{
					Com_Printf("\tController %d initialized\n", gLaunchController);
					startsetMainController(gLaunchController);
				}
				Cvar_SetValue( "inSplashMenu", 0 );
				Cvar_SetValue( "ControllerOutNum", -1 );
				XBLog_Write("STEFX: direct-map input gate cleared splash/controller lock");
			}
			else
#endif
			if( quickStart )
			{
				Com_Printf("\tController %d initialized\n", gLaunchController);
				startsetMainController(gLaunchController);

				// We're bypassing splash menu!
				Cvar_SetValue( "inSplashMenu", 0 );
			}

			// Only do this check once, no matter what:
			first = qfalse;
		}

		if ( mainControllerDelayedUnplug && CurrentStateIsInteractive() && ControllerOutNum.integer < 0)
			IN_ProcessChanges(0, mainControllerDelayedUnplug);

		// Generate callbacks for each controller that's plugged in
		for (int port = 0; port < IN_MAX_CONTROLLERS; ++port)
		{
			if (xboxTraceInput) XBLF("JA: IN_TIGHT #%d port=%d handle=%p", callCount - 1, port, (void*)in_state->controllers[port].handle);
			if (in_state->controllers[port].handle)
				IN_UpdateGamepad(port);
		}

		if (xboxTraceInput) XBLF("JA: IN_TIGHT #%d before IN_UIEmptyQueue", callCount - 1);
		IN_UIEmptyQueue();
		if (xboxTraceInput) XBLF("JA: IN_TIGHT #%d before IN_RumbleFrame", callCount - 1);
		IN_RumbleFrame();
		if (xboxTraceInput) XBLF("JA: IN_TIGHT #%d exit", callCount - 1);
	}
}
