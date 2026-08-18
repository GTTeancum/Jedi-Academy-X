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

#if defined(_XBOX) && SP_XBOX_SMOKE_AUTOMATION
#define XBOX_SMOKE_MAX_BUTTONS 64
struct xboxSmokeButtonEvent_t
{
	int atMs;
	int holdMs;
	int controller;
	fakeAscii_t button;
	bool waitForUi;
	bool rawKey;
	bool pressed;
	bool released;
	char name[16];
};

static xboxSmokeButtonEvent_t xboxSmokeButtons[XBOX_SMOKE_MAX_BUTTONS];
static int xboxSmokeButtonCount = 0;
static bool xboxSmokeButtonsLoaded = false;
static int xboxSmokeButtonStartMs = 0;
static int xboxSmokeButtonUiStartMs = 0;

static fakeAscii_t IN_XboxSmokeButtonFromName(const char *name)
{
	if (!name || !name[0]) return A_NULL;
	if (!Q_stricmp(name, "start")) return A_JOY4;
	if (!Q_stricmp(name, "back")) return A_JOY1;
	if (!Q_stricmp(name, "a")) return A_JOY15;
	if (!Q_stricmp(name, "b")) return A_JOY14;
	if (!Q_stricmp(name, "x")) return A_JOY16;
	if (!Q_stricmp(name, "y")) return A_JOY13;
	if (!Q_stricmp(name, "up") || !Q_stricmp(name, "dpad_up")) return A_JOY5;
	if (!Q_stricmp(name, "down") || !Q_stricmp(name, "dpad_down")) return A_JOY7;
	if (!Q_stricmp(name, "left") || !Q_stricmp(name, "dpad_left")) return A_JOY8;
	if (!Q_stricmp(name, "right") || !Q_stricmp(name, "dpad_right")) return A_JOY6;
	if (!Q_stricmp(name, "white")) return A_JOY9;
	if (!Q_stricmp(name, "black")) return A_JOY10;
	if (!Q_stricmp(name, "lt") || !Q_stricmp(name, "ltrigger")) return A_JOY11;
	if (!Q_stricmp(name, "rt") || !Q_stricmp(name, "rtrigger")) return A_JOY12;
	if (!Q_stricmp(name, "lstick")) return A_JOY2;
	if (!Q_stricmp(name, "rstick")) return A_JOY3;
	return A_NULL;
}

static fakeAscii_t IN_XboxSmokeRawKeyFromName(const char *name)
{
	if (!name || !name[0]) return A_NULL;
	if (!Q_stricmp(name, "ui_accept") || !Q_stricmp(name, "ui_mouse1")) return A_MOUSE1;
	if (!Q_stricmp(name, "ui_enter")) return A_ENTER;
	if (!Q_stricmp(name, "ui_back") || !Q_stricmp(name, "ui_escape")) return A_ESCAPE;
	if (!Q_stricmp(name, "ui_up")) return A_CURSOR_UP;
	if (!Q_stricmp(name, "ui_down")) return A_CURSOR_DOWN;
	if (!Q_stricmp(name, "ui_left")) return A_CURSOR_LEFT;
	if (!Q_stricmp(name, "ui_right")) return A_CURSOR_RIGHT;
	return A_NULL;
}

static void IN_XboxSmokeLoadButtons(void)
{
	if (xboxSmokeButtonsLoaded)
	{
		return;
	}
	xboxSmokeButtonsLoaded = true;
	xboxSmokeButtonStartMs = Sys_Milliseconds();

	FILE *f = fopen("D:\\ja_sp_buttons.txt", "r");
	if (!f)
	{
		return;
	}

	char line[128];
	while (xboxSmokeButtonCount < XBOX_SMOKE_MAX_BUTTONS && fgets(line, sizeof(line), f))
	{
		char atToken[24];
		char buttonName[16];
		int atMs = 0;
		int holdMs = 160;
		int controller = 0;
		bool waitForUi = false;
		atToken[0] = '\0';
		buttonName[0] = '\0';

		char *cursor = line;
		while (*cursor == ' ' || *cursor == '\t') cursor++;
		if (!*cursor || *cursor == '#')
		{
			continue;
		}

		int fields = sscanf(cursor, "%23s %15s %d %d", atToken, buttonName, &holdMs, &controller);
		if (fields < 2)
		{
			XBLog_Writef("JA: SMOKE_BUTTON invalid line '%s'", cursor);
			continue;
		}
		if ((atToken[0] == 'u' || atToken[0] == 'U') &&
			(atToken[1] == 'i' || atToken[1] == 'I') &&
			atToken[2] == '+')
		{
			waitForUi = true;
			atMs = atoi(atToken + 3);
		}
		else
		{
			atMs = atoi(atToken);
		}
		if (fields < 3 || holdMs <= 0)
		{
			holdMs = 160;
		}
		if (fields < 4 || controller < 0 || controller >= IN_MAX_CONTROLLERS)
		{
			controller = 0;
		}

		bool rawKey = false;
		fakeAscii_t button = IN_XboxSmokeButtonFromName(buttonName);
		if (button == A_NULL)
		{
			button = IN_XboxSmokeRawKeyFromName(buttonName);
			rawKey = (button != A_NULL);
			if (button == A_NULL)
			{
				XBLog_Writef("JA: SMOKE_BUTTON unknown button '%s'", buttonName);
				continue;
			}
		}

		xboxSmokeButtonEvent_t *event = &xboxSmokeButtons[xboxSmokeButtonCount++];
		event->atMs = atMs;
		event->holdMs = holdMs;
		event->controller = controller;
		event->button = button;
		event->waitForUi = waitForUi;
		event->rawKey = rawKey;
		event->pressed = false;
		event->released = false;
		Q_strncpyz(event->name, buttonName, sizeof(event->name));
	}
	fclose(f);

	g_SPXBSmokeButtonCount = (unsigned int)xboxSmokeButtonCount;
	g_SPXBSmokeButtonPressCount = 0;
	g_SPXBSmokeButtonReleaseCount = 0;
	g_SPXBSmokeButtonUiStartMs = 0;
	g_SPXBSmokeButtonLast = 0;
	XBLog_Writef("JA: SMOKE_BUTTON loaded count=%d startMs=%d", xboxSmokeButtonCount, xboxSmokeButtonStartMs);
}

static void IN_XboxSmokeFrame(void)
{
	IN_XboxSmokeLoadButtons();
	if (xboxSmokeButtonCount <= 0)
	{
		return;
	}

	const int now = Sys_Milliseconds();
	if (xboxSmokeButtonUiStartMs == 0 && (cls.keyCatchers & KEYCATCH_UI))
	{
		xboxSmokeButtonUiStartMs = now;
		g_SPXBSmokeButtonUiStartMs = (unsigned int)xboxSmokeButtonUiStartMs;
		XBLog_Writef("JA: SMOKE_BUTTON ui-ready startMs=%d state=%d catchers=0x%x",
			xboxSmokeButtonUiStartMs,
			(int)cls.state,
			(unsigned int)cls.keyCatchers);
	}

	for (int i = 0; i < xboxSmokeButtonCount; ++i)
	{
		xboxSmokeButtonEvent_t *event = &xboxSmokeButtons[i];
		if (event->waitForUi && xboxSmokeButtonUiStartMs == 0)
		{
			continue;
		}
		const int baseMs = event->waitForUi ? xboxSmokeButtonUiStartMs : xboxSmokeButtonStartMs;
		const int elapsed = now - baseMs;
		if (!event->pressed && elapsed >= event->atMs)
		{
			++g_SPXBSmokeButtonPressCount;
			g_SPXBSmokeButtonLast = ((unsigned int)event->button & 0xffffu) |
				(event->rawKey ? 0x00010000u : 0u) |
				(event->waitForUi ? 0x00020000u : 0u) |
				(((unsigned int)i & 0xffu) << 24);
			XBLog_Writef("JA: SMOKE_BUTTON press t=%d idx=%d controller=%d button=%s raw=%d ui=%d", elapsed, i, event->controller, event->name, event->rawKey ? 1 : 0, event->waitForUi ? 1 : 0);
			if (event->rawKey)
			{
				Sys_QueEvent(0, SE_KEY, event->button, true, 0, NULL);
			}
			else
			{
				IN_CommonJoyPress(event->controller, event->button, true);
			}
			event->pressed = true;
		}
		if (event->pressed && !event->released && elapsed >= event->atMs + event->holdMs)
		{
			++g_SPXBSmokeButtonReleaseCount;
			g_SPXBSmokeButtonLast = ((unsigned int)event->button & 0xffffu) |
				(event->rawKey ? 0x00010000u : 0u) |
				(event->waitForUi ? 0x00020000u : 0u) |
				0x00800000u |
				(((unsigned int)i & 0xffu) << 24);
			XBLog_Writef("JA: SMOKE_BUTTON release t=%d idx=%d controller=%d button=%s raw=%d ui=%d", elapsed, i, event->controller, event->name, event->rawKey ? 1 : 0, event->waitForUi ? 1 : 0);
			if (event->rawKey)
			{
				Sys_QueEvent(0, SE_KEY, event->button, false, 0, NULL);
			}
			else
			{
				IN_CommonJoyPress(event->controller, event->button, false);
			}
			event->released = true;
		}
	}
}
#endif



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
			IN_PadUnplugged(port);

		}

		// Open insertions.
		if( (1 << port) & dwInsert )
		{
			in_state->controllers[port].handle = XInputOpen( XDEVICE_TYPE_GAMEPAD, port, XDEVICE_NO_SLOT, NULL );
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
		IN_ProcessChanges( XGetDevices( XDEVICE_TYPE_GAMEPAD ), 0 );

		IN_RumbleInit();
	}

static inline float _joyAxisConvert(SHORT x)
{
	// Change scale
	float y = x / 32767.0;

	// Cheesy deadzone
	if(fabs(y) < 0.25f)
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

void IN_UpdateGamepad(int port)
{
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

	// Get old state
	XINPUT_STATE &oldState(in_state->controllers[port].state);

	int buttonIdx;
	bool oldPressed, newPressed;

	// Check all digital buttons first
	for (buttonIdx = 0; buttonIdx < IN_NUM_DIGITAL_BUTTONS; ++buttonIdx)
	{
		oldPressed = oldState.Gamepad.wButtons & (1 << buttonIdx);
		newPressed = newState.Gamepad.wButtons & (1 << buttonIdx);

		if (oldPressed != newPressed)
			IN_CommonJoyPress(port, digitalXlat[buttonIdx], newPressed);
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
#if defined(_XBOX) && SP_XBOX_SMOKE_AUTOMATION
		IN_XboxSmokeFrame();
#endif

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
			if( Sys_QuickStart() )
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
