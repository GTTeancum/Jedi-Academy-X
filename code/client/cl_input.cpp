// cl.input.c  -- builds an intended movement command to send to the server

// leave this as first line for PCH reasons...
//
#include "../server/exe_headers.h"


#include "client.h"
#include "client_ui.h"
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
#include "../win32/xb_log.h"
extern void Key_SetBinding( int keynum, const char *binding );
extern char *Key_GetBinding( int keynum );
#endif

#ifdef _XBOX
#include "cl_input_hotswap.h"
#endif

unsigned	frame_msec;
int			old_com_frameTime;
float cl_mPitchOverride = 0.0f;
float cl_mYawOverride = 0.0f;

/*
===============================================================================

KEY BUTTONS

Continuous button event tracking is complicated by the fact that two different
input sources (say, mouse button 1 and the control key) can both press the
same button, but the button should only be released when both of the
pressing key have been released.

When a key event issues a button command (+forward, +attack, etc), it appends
its key number as argv(1) so it can be matched up with the release.

argv(2) will be set to the time the event happened, which allows exact
control even at low framerates when the down and up events may both get qued
at the same time.

===============================================================================
*/


kbutton_t	in_left, in_right, in_forward, in_back;
kbutton_t	in_lookup, in_lookdown, in_moveleft, in_moveright;
kbutton_t	in_strafe, in_speed;
kbutton_t	in_up, in_down;

kbutton_t	in_buttons[9];


qboolean	in_mlooking;


#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
static void STEFX_SetDefaultXboxBinding( int keynum, const char *binding )
{
	const char *existing = Key_GetBinding( keynum );

	if ( existing && existing[0] && !Q_stricmp( existing, binding ) )
	{
		XBLF("STEFX: confirmed Xbox bind %s -> %s",
			Key_KeynumToString( keynum ),
			existing);
		return;
	}

	if ( existing && existing[0] )
	{
		XBLF("STEFX: replaced Xbox bind %s old='%s' new='%s'",
			Key_KeynumToString( keynum ),
			existing,
			binding);
	}
	else
	{
		XBLF("STEFX: installed Xbox bind %s -> %s",
			Key_KeynumToString( keynum ),
			binding);
	}
	Key_SetBinding( keynum, binding );
}

static void STEFX_InstallDefaultXboxBindings( void )
{
	static qboolean s_installed = qfalse;

	if ( s_installed )
	{
		return;
	}
	s_installed = qtrue;

	STEFX_SetDefaultXboxBinding( A_JOY12, "+attack" );              // Right trigger
	STEFX_SetDefaultXboxBinding( A_JOY10, "+altattack" );           // Black button
	STEFX_SetDefaultXboxBinding( A_JOY11, "+moveup" );              // Left trigger
	STEFX_SetDefaultXboxBinding( A_JOY9, "+movedown" );             // White button
	STEFX_SetDefaultXboxBinding( A_JOY15, "+use" );                 // A
	STEFX_SetDefaultXboxBinding( A_JOY14, "+button2" );             // B, mission info / use item
	STEFX_SetDefaultXboxBinding( A_JOY16, "toggle cl_run" );        // X
	STEFX_SetDefaultXboxBinding( A_JOY13, "centerview; zoomoff" );  // Y
	STEFX_SetDefaultXboxBinding( A_JOY5, "+zoom" );                 // D-pad up
	STEFX_SetDefaultXboxBinding( A_JOY7, "zoomoff" );               // D-pad down
	STEFX_SetDefaultXboxBinding( A_JOY8, "weapprev" );              // D-pad left
	STEFX_SetDefaultXboxBinding( A_JOY6, "weapnext" );              // D-pad right
	STEFX_SetDefaultXboxBinding( A_JOY1, "datapad" );               // Back
	STEFX_SetDefaultXboxBinding( A_JOY2, "+use" );                  // Left stick click, lean modifier
	STEFX_SetDefaultXboxBinding( A_JOY3, "toggle cg_thirdperson" ); // Right stick click
}

static cvar_t *stefx_smokeInput;
static cvar_t *stefx_smokeInputStart;
static cvar_t *stefx_smokeInputEnd;
static cvar_t *stefx_smokeInputForward;
static cvar_t *stefx_smokeInputSide;
static cvar_t *stefx_smokeInputYaw;
static cvar_t *stefx_smokeInputAttackStart;
static cvar_t *stefx_smokeInputAttackEnd;

static qboolean STEFX_ViewAnglesBad( const vec3_t angles )
{
	return (qboolean)(IS_NAN(angles[0]) || IS_NAN(angles[1]) || IS_NAN(angles[2]));
}

static void STEFX_SeedClientViewAnglesIfInvalid( void )
{
	static int s_logBudget = 8;

	if ( !STEFX_ViewAnglesBad( cl.viewangles ) )
	{
		return;
	}

	VectorCopy( cl.frame.ps.viewangles, cl.viewangles );
	if ( STEFX_ViewAnglesBad( cl.viewangles ) )
	{
		VectorClear( cl.viewangles );
	}

	if ( s_logBudget > 0 )
	{
		XBLF("STEFX: seeded invalid client viewangles from snapshot view=(%g,%g,%g) ps=(%g,%g,%g)",
			cl.viewangles[0], cl.viewangles[1], cl.viewangles[2],
			cl.frame.ps.viewangles[0], cl.frame.ps.viewangles[1], cl.frame.ps.viewangles[2]);
		--s_logBudget;
	}
}

static void STEFX_InitSmokeInputCvars( void )
{
	stefx_smokeInput = Cvar_Get( "stefx_smoke_input", "0", CVAR_TEMP );
	stefx_smokeInputStart = Cvar_Get( "stefx_smoke_input_start", "18000", CVAR_TEMP );
	stefx_smokeInputEnd = Cvar_Get( "stefx_smoke_input_end", "26000", CVAR_TEMP );
	stefx_smokeInputForward = Cvar_Get( "stefx_smoke_input_forward", "90", CVAR_TEMP );
	stefx_smokeInputSide = Cvar_Get( "stefx_smoke_input_side", "0", CVAR_TEMP );
	stefx_smokeInputYaw = Cvar_Get( "stefx_smoke_input_yaw", "0", CVAR_TEMP );
	stefx_smokeInputAttackStart = Cvar_Get( "stefx_smoke_input_attack_start", "19000", CVAR_TEMP );
	stefx_smokeInputAttackEnd = Cvar_Get( "stefx_smoke_input_attack_end", "23000", CVAR_TEMP );

	XBL("STEFX: smoke input cvars registered; diagnostic input is off by default");
}

static void STEFX_ApplySmokeInput( usercmd_t *cmd )
{
	static int s_logBudget = 48;
	const int serverTime = cmd ? cmd->serverTime : 0;
	int startTime;
	int endTime;
	int attackStart;
	int attackEnd;
	int forwardMove;
	int sideMove;
	int yawMove;
	qboolean active;
	qboolean attacking;

	if ( !cmd || !stefx_smokeInput || !stefx_smokeInput->integer )
	{
		return;
	}

	startTime = stefx_smokeInputStart ? stefx_smokeInputStart->integer : 18000;
	endTime = stefx_smokeInputEnd ? stefx_smokeInputEnd->integer : 26000;
	attackStart = stefx_smokeInputAttackStart ? stefx_smokeInputAttackStart->integer : 19000;
	attackEnd = stefx_smokeInputAttackEnd ? stefx_smokeInputAttackEnd->integer : 23000;
	forwardMove = stefx_smokeInputForward ? stefx_smokeInputForward->integer : 90;
	sideMove = stefx_smokeInputSide ? stefx_smokeInputSide->integer : 0;
	yawMove = stefx_smokeInputYaw ? stefx_smokeInputYaw->integer : 0;

	active = ( serverTime >= startTime && ( endTime <= 0 || serverTime <= endTime ) );
	if ( !active )
	{
		return;
	}

	if ( !forwardMove && !sideMove )
	{
		forwardMove = 90;
	}
	if ( attackEnd > 0 && attackEnd < startTime )
	{
		attackStart = startTime + 1000;
		attackEnd = ( endTime > attackStart ) ? endTime : attackStart + 4000;
	}

	cmd->forwardmove = ClampChar( cmd->forwardmove + forwardMove );
	cmd->rightmove = ClampChar( cmd->rightmove + sideMove );
	if ( yawMove )
	{
		cl.viewangles[YAW] += (float)yawMove;
		cmd->angles[YAW] = ANGLE2SHORT( cl.viewangles[YAW] );
	}

	attacking = ( serverTime >= attackStart && ( attackEnd <= 0 || serverTime <= attackEnd ) );
	if ( attacking )
	{
		cmd->buttons |= BUTTON_ATTACK;
	}

	if ( s_logBudget > 0 )
	{
		Com_PrintfAlways( "STEFX: smoke input applied serverTime=%d window=%d..%d attackWindow=%d..%d configuredInput=(%d,%d,%d) move=(%d,%d,%d) attack=%d buttons=0x%x weapon=%d\n",
			serverTime,
			startTime,
			endTime,
			attackStart,
			attackEnd,
			forwardMove,
			sideMove,
			yawMove,
			cmd->forwardmove,
			cmd->rightmove,
			cmd->upmove,
			attacking ? 1 : 0,
			cmd->buttons,
			cmd->weapon );
		s_logBudget--;
	}
}
#endif


#ifdef _XBOX
HotSwapManager swapMan1(HOTSWAP_ID_WHITE);
HotSwapManager swapMan2(HOTSWAP_ID_BLACK);
HotSwapManager swapMan3(HOTSWAP_ID_YELLOW);


void IN_HotSwap1On(void)
{
	swapMan1.SetDown();
}

void IN_HotSwap2On(void)
{
	swapMan2.SetDown();
}

void IN_HotSwap3On(void)
{
	swapMan3.SetDown();
}


void IN_HotSwap1Off(void)
{
	swapMan1.SetUp();
}

void IN_HotSwap2Off(void)
{
	swapMan2.SetUp();
}

void IN_HotSwap3Off(void)
{
	swapMan3.SetUp();
}


void CL_UpdateHotSwap(void)
{
	swapMan1.Update();
	swapMan2.Update();
	swapMan3.Update();
}


bool CL_ExtendSelectTime(void)
{
	return swapMan1.ButtonDown() || swapMan2.ButtonDown() || swapMan3.ButtonDown();
}
#endif


static void IN_UseGivenForce(void)
{
	char *c = Cmd_Argv(1);
	int forceNum=-1;
	int genCmdNum = 0;

	if(c) {
		forceNum = atoi(c);
	} else {
		return;
	}

	switch(forceNum) {
	case FP_DRAIN:
		genCmdNum = GENCMD_FORCE_DRAIN;
		break;
	case FP_PUSH:
		genCmdNum = GENCMD_FORCE_THROW;
		break;
	case FP_SPEED:
		genCmdNum = GENCMD_FORCE_SPEED;
		break;
	case FP_PULL:
		genCmdNum = GENCMD_FORCE_PULL;
		break;
	case FP_TELEPATHY:
		genCmdNum = GENCMD_FORCE_DISTRACT;
		break;
	case FP_GRIP:
		genCmdNum = GENCMD_FORCE_GRIP;
		break;
	case FP_LIGHTNING:
		genCmdNum = GENCMD_FORCE_LIGHTNING;
		break;
	case FP_RAGE:
		genCmdNum = GENCMD_FORCE_RAGE;
		break;
	case FP_PROTECT:
		genCmdNum = GENCMD_FORCE_PROTECT;
		break;
	case FP_ABSORB:
		genCmdNum = GENCMD_FORCE_ABSORB;
		break;
	case FP_SEE:
		genCmdNum = GENCMD_FORCE_SEEING;
		break;
	case FP_HEAL:
		genCmdNum = GENCMD_FORCE_HEAL;
		break;
	default:
		assert(0);
		break;
	}

	if(genCmdNum) {
		cl.gcmdSendValue = qtrue;
		cl.gcmdValue = genCmdNum;
	}
}


void IN_MLookDown( void ) {
	in_mlooking = qtrue;
}

void IN_MLookUp( void ) {
	in_mlooking = qfalse;
	if ( !cl_freelook->integer ) {
		IN_CenterView ();
	}
}

void IN_KeyDown( kbutton_t *b ) {
	int		k;
	char	*c;
	
	c = Cmd_Argv(1);
	if ( c[0] ) {
		k = atoi(c);
	} else {
		k = -1;		// typed manually at the console for continuous down
	}

	if ( k == b->down[0] || k == b->down[1] ) {
		return;		// repeating key
	}
	
	if ( !b->down[0] ) {
		b->down[0] = k;
	} else if ( !b->down[1] ) {
		b->down[1] = k;
	} else {
		Com_Printf ("Three keys down for a button!\n");
		return;
	}
	
	if ( b->active ) {
		return;		// still down
	}

	// save timestamp for partial frame summing
	c = Cmd_Argv(2);
	b->downtime = atoi(c);

	b->active = qtrue;
	b->wasPressed = qtrue;
}

void IN_KeyUp( kbutton_t *b ) {
	int		k;
	char	*c;
	unsigned	uptime;

	c = Cmd_Argv(1);
	if ( c[0] ) {
		k = atoi(c);
	} else {
		// typed manually at the console, assume for unsticking, so clear all
		b->down[0] = b->down[1] = 0;
		b->active = qfalse;
		return;
	}

	if ( b->down[0] == k ) {
		b->down[0] = 0;
	} else if ( b->down[1] == k ) {
		b->down[1] = 0;
	} else {
		return;		// key up without coresponding down (menu pass through)
	}
	if ( b->down[0] || b->down[1] ) {
		return;		// some other key is still holding it down
	}

	b->active = qfalse;

	// save timestamp for partial frame summing
	c = Cmd_Argv(2);
	uptime = atoi(c);
	if ( uptime ) {
		b->msec += uptime - b->downtime;
	} else {
		b->msec += frame_msec / 2;
	}

	b->active = qfalse;
}



/*
===============
CL_KeyState

Returns the fraction of the frame that the key was down
===============
*/
float CL_KeyState( kbutton_t *key ) {
	float		val;
	int			msec;

	msec = key->msec;
	key->msec = 0;

	if ( key->active ) {
		// still down
		if ( !key->downtime ) {
			msec = com_frameTime;
		} else {
			msec += com_frameTime - key->downtime;
		}
		key->downtime = com_frameTime;
	}

#if 0
	if (msec) {
		Com_Printf ("%i ", msec);
	}
#endif

	val = (float)msec / frame_msec;
	if ( val < 0 ) {
		val = 0;
	}
	if ( val > 1 ) {
		val = 1;
	}

	return val;
}



void IN_UpDown(void) {IN_KeyDown(&in_up);}
void IN_UpUp(void) {IN_KeyUp(&in_up);}
void IN_DownDown(void) {IN_KeyDown(&in_down);}
void IN_DownUp(void) {IN_KeyUp(&in_down);}
void IN_LeftDown(void) {IN_KeyDown(&in_left);}
void IN_LeftUp(void) {IN_KeyUp(&in_left);}
void IN_RightDown(void) {IN_KeyDown(&in_right);}
void IN_RightUp(void) {IN_KeyUp(&in_right);}
void IN_ForwardDown(void) {IN_KeyDown(&in_forward);}
void IN_ForwardUp(void) {IN_KeyUp(&in_forward);}
void IN_BackDown(void) {IN_KeyDown(&in_back);}
void IN_BackUp(void) {IN_KeyUp(&in_back);}
void IN_LookupDown(void) {IN_KeyDown(&in_lookup);}
void IN_LookupUp(void) {IN_KeyUp(&in_lookup);}
void IN_LookdownDown(void) {IN_KeyDown(&in_lookdown);}
void IN_LookdownUp(void) {IN_KeyUp(&in_lookdown);}
void IN_MoveleftDown(void) {IN_KeyDown(&in_moveleft);}
void IN_MoveleftUp(void) {IN_KeyUp(&in_moveleft);}
void IN_MoverightDown(void) {IN_KeyDown(&in_moveright);}
void IN_MoverightUp(void) {IN_KeyUp(&in_moveright);}

void IN_SpeedDown(void) {IN_KeyDown(&in_speed);}
void IN_SpeedUp(void) {IN_KeyUp(&in_speed);}
void IN_StrafeDown(void) {IN_KeyDown(&in_strafe);}
void IN_StrafeUp(void) {IN_KeyUp(&in_strafe);}

void IN_Button0Down(void) {IN_KeyDown(&in_buttons[0]);}
void IN_Button0Up(void) {IN_KeyUp(&in_buttons[0]);}
void IN_Button1Down(void) {IN_KeyDown(&in_buttons[1]);}
void IN_Button1Up(void) {IN_KeyUp(&in_buttons[1]);}
void IN_Button2Down(void) {IN_KeyDown(&in_buttons[2]);}
void IN_Button2Up(void) {IN_KeyUp(&in_buttons[2]);}
void IN_Button3Down(void) {IN_KeyDown(&in_buttons[3]);}
void IN_Button3Up(void) {IN_KeyUp(&in_buttons[3]);}
void IN_Button4Down(void) {IN_KeyDown(&in_buttons[4]);}
void IN_Button4Up(void) {IN_KeyUp(&in_buttons[4]);}
void IN_Button5Down(void) {IN_KeyDown(&in_buttons[5]);}
void IN_Button5Up(void) {IN_KeyUp(&in_buttons[5]);}
void IN_Button6Down(void) {IN_KeyDown(&in_buttons[6]);}
void IN_Button6Up(void) {IN_KeyUp(&in_buttons[6]);}
void IN_Button7Down(void) {IN_KeyDown(&in_buttons[7]);}
void IN_Button7Up(void) {IN_KeyUp(&in_buttons[7]);}
void IN_Button8Down(void) {IN_KeyDown(&in_buttons[8]);}
void IN_Button8Up(void) {IN_KeyUp(&in_buttons[8]);}


void IN_CenterView (void) {
	cl.viewangles[PITCH] = -SHORT2ANGLE(cl.frame.ps.delta_angles[PITCH]);
}


//==========================================================================

cvar_t	*cl_upspeed;
cvar_t	*cl_forwardspeed;
cvar_t	*cl_sidespeed;

cvar_t	*cl_yawspeed;
cvar_t	*cl_pitchspeed;

cvar_t	*cl_run;

cvar_t	*cl_anglespeedkey;


/*
================
CL_AdjustAngles

Moves the local angle positions
================
*/
/*
void CL_AdjustAngles( void ) {
	float	speed;
	
	if ( in_speed.active ) {
		speed = 0.001 * cls.frametime * cl_anglespeedkey->value;
	} else {
		speed = 0.001 * cls.frametime;
	}

	if ( !in_strafe.active ) {
		if ( cl_mYawOverride )
		{
			cl.viewangles[YAW] -= cl_mYawOverride*5.0f*speed*cl_yawspeed->value*CL_KeyState (&in_right);
			cl.viewangles[YAW] += cl_mYawOverride*5.0f*speed*cl_yawspeed->value*CL_KeyState (&in_left);
		}
		else
		{
			cl.viewangles[YAW] -= speed*cl_yawspeed->value*CL_KeyState (&in_right);
			cl.viewangles[YAW] += speed*cl_yawspeed->value*CL_KeyState (&in_left);
		}
	}

	if ( cl_mPitchOverride )
	{
		cl.viewangles[PITCH] -= cl_mPitchOverride*5.0f*speed*cl_pitchspeed->value * CL_KeyState (&in_lookup);
		cl.viewangles[PITCH] += cl_mPitchOverride*5.0f*speed*cl_pitchspeed->value * CL_KeyState (&in_lookdown);
	}
	else
	{
		cl.viewangles[PITCH] -= speed*cl_pitchspeed->value * CL_KeyState (&in_lookup);
		cl.viewangles[PITCH] += speed*cl_pitchspeed->value * CL_KeyState (&in_lookdown);
	}
}
*/

/*
================
CL_KeyMove

Sets the usercmd_t based on key states
================
*/
void CL_KeyMove( usercmd_t *cmd ) {
	int		movespeed;
	int		forward, side, up;

	//
	// adjust for speed key / running
	// the walking flag is to keep animations consistant
	// even during acceleration and develeration
	//
	if ( in_speed.active ^ cl_run->integer ) {
		movespeed = 127;
		cmd->buttons &= ~BUTTON_WALKING;
	} else {
		cmd->buttons |= BUTTON_WALKING;
		movespeed = 64;
	}

	forward = 0;
	side = 0;
	up = 0;
	if ( in_strafe.active ) {
		side += movespeed * CL_KeyState (&in_right);
		side -= movespeed * CL_KeyState (&in_left);
	}

	side += movespeed * CL_KeyState (&in_moveright);
	side -= movespeed * CL_KeyState (&in_moveleft);


	up += movespeed * CL_KeyState (&in_up);
	up -= movespeed * CL_KeyState (&in_down);

	forward += movespeed * CL_KeyState (&in_forward);
	forward -= movespeed * CL_KeyState (&in_back);

	cmd->forwardmove = ClampChar( forward );
	cmd->rightmove = ClampChar( side );
	cmd->upmove = ClampChar( up );
}

void _UI_MouseEvent( int dx, int dy );

/*
=================
CL_MouseEvent
=================
*/
void CL_MouseEvent( int dx, int dy, int time ) {
	if ( cls.keyCatchers & KEYCATCH_UI ) {
		_UI_MouseEvent( dx, dy );
	}
	else {
		cl.mouseDx[cl.mouseIndex] += dx;
		cl.mouseDy[cl.mouseIndex] += dy;
	}
}

/*
=================
CL_JoystickEvent

Joystick values stay set until changed
=================
*/
void CL_JoystickEvent( int axis, int value, int time ) {
	if ( axis < 0 || axis >= MAX_JOYSTICK_AXIS ) {
		Com_Error( ERR_DROP, "CL_JoystickEvent: bad axis %i", axis );
	}
	cl.joystickAxis[axis] = value;
}

/*
=================
CL_JoystickMove
=================
*/
void CL_JoystickMove( usercmd_t *cmd )
{
	cmd->rightmove = ClampChar( cmd->rightmove + cl.joystickAxis[AXIS_SIDE] );
	cmd->forwardmove = ClampChar( cmd->forwardmove + cl.joystickAxis[AXIS_FORWARD] );
	cmd->upmove = ClampChar( cmd->upmove + cl.joystickAxis[AXIS_UP] );

	// Smarter run-speed detection, taking diagonals into account!
	if( (cmd->forwardmove * cmd->forwardmove + cmd->rightmove * cmd->rightmove) < (MOVE_RUN * MOVE_RUN) )
		cmd->buttons |= BUTTON_WALKING;
}

/*
=================
CL_MouseMove
=================
*/
#ifdef _XBOX
void CL_MouseClamp(int *x, int *y)
{
	float ax = Q_fabs(*x);
	float ay = Q_fabs(*y);

	ax = (ax-10)*(3.0f/45.0f) * (ax-10) * (Q_fabs(*x) > 10);
	ay = (ay-10)*(3.0f/45.0f) * (ay-10) * (Q_fabs(*y) > 10);
	if (*x < 0)
		*x = -ax;
	else
		*x = ax;
	if (*y < 0)
		*y = -ay;
	else
		*y = ay;
}
#endif

extern short cg_crossHairStatus;

void CL_MouseMove( usercmd_t *cmd )
{
	const float mouseSpeedX = 0.06f;
	const float mouseSpeedY = 0.05f;
	const float m_hoverSensitivity = 0.4f;

	const float	speed = static_cast<float>(frame_msec);
	const float pitch = m_pitch->value;

	// Get raw stick values:
	int ax = cl.mouseDx[cl.mouseIndex];
	int ay = cl.mouseDy[cl.mouseIndex];
	// Run them through our filter:
	CL_MouseClamp(&ax, &ay);

	// Adjust for frame duration and make horizontal slightly faster:
	float mx = ax * speed * mouseSpeedX;
	float my = ay * speed * mouseSpeedY;
	// Save these before we start tuning them, in case user is on a vehicle with overrides:
	float rawMx = mx;
	float rawMy = my;

	// Slow it down when targeting an enemy:
	if (cg_crossHairStatus == 1)
	{
		mx *= m_hoverSensitivity;
		my *= m_hoverSensitivity;
	}

	// Switch entries, clear values:
	cl.mouseIndex ^= 1;
	cl.mouseDx[cl.mouseIndex] = 0;
	cl.mouseDy[cl.mouseIndex] = 0;

	// scale by FOV and user settings:
	mx *= cl_sensitivity->value * cl.cgameSensitivity;

	// note the capital Y in the cvarname - scale by FOV and settings:
	my *= cl_sensitivityY->value * cl.cgameSensitivity;

	if (!mx && !my) {
		// If there was a movement but no change in angles then start auto-leveling the camera
		extern int g_lastFireTime;
		float autolevelSpeed = 0.03f;

		if (cg_crossHairStatus != 1 &&							// Not looking at an enemy
			cl.joystickAxis[AXIS_FORWARD] &&					// Moving forward/backward
			cl.frame.ps.groundEntityNum != ENTITYNUM_NONE &&	// Not in the air
			Cvar_VariableIntegerValue("cl_autolevel") &&		// Autolevel is turned on
			g_lastFireTime < Sys_Milliseconds() - 1000)			// Haven't fired recently
		{
			float normAngle = -SHORT2ANGLE(cl.frame.ps.delta_angles[PITCH]);
			// The adjustment to normAngle below is meant to add or remove some multiple
			// of 360, so that normAngle is within 180 of viewangles[PITCH]. It should
			// be correct.
			int diff = (int)(cl.viewangles[PITCH] - normAngle);
			if (diff > 180)
				normAngle += 360.0f * ((diff+180) / 360);
			else if (diff < -180)
				normAngle -= 360.0f * ((-diff+180) / 360);

			if (Cvar_VariableIntegerValue("cg_thirdperson") == 1)
			{
//				normAngle += 10;	// Removed by BTO, 2003/05/14, I hate it
				autolevelSpeed *= 1.5f;
			}
			if (cl.viewangles[PITCH] > normAngle)
			{
				cl.viewangles[PITCH] -= autolevelSpeed * speed;
				if (cl.viewangles[PITCH] < normAngle) cl.viewangles[PITCH] = normAngle;
			}
			else if (cl.viewangles[PITCH] < normAngle)
			{
				cl.viewangles[PITCH] += autolevelSpeed * speed;
				if (cl.viewangles[PITCH] > normAngle) cl.viewangles[PITCH] = normAngle;
			}
		}
		return;
	}

	// Do yaw - use un-tweaked number if override is active:
	if ( cl_mYawOverride )
		cl.viewangles[YAW] -= cl_mYawOverride * rawMx;
	else
		cl.viewangles[YAW] -= m_yaw->value * mx;

	// Do pitch - use un-tweaked number if override is active:
	const float cl_pitchSensitivity = 0.5f;	// Should be a cvar!
	if ( cl_mPitchOverride )
	{
		if ( pitch > 0 )
			cl.viewangles[PITCH] += cl_mPitchOverride * rawMy * cl_pitchSensitivity;
		else
			cl.viewangles[PITCH] -= cl_mPitchOverride * rawMy * cl_pitchSensitivity;
	}
	else
	{
		cl.viewangles[PITCH] += pitch * my * cl_pitchSensitivity;
	}
}


/*
==============
CL_CmdButtons
==============
*/
void CL_CmdButtons( usercmd_t *cmd ) {
	int		i;

	//
	// figure button bits
	// send a button bit even if the key was pressed and released in
	// less than a frame
	//	
	for (i = 0 ; i < 9 ; i++) {
		if ( in_buttons[i].active || in_buttons[i].wasPressed ) {
			cmd->buttons |= 1 << i;
		}
		in_buttons[i].wasPressed = qfalse;
	}

	if ( cls.keyCatchers ) {
		//cmd->buttons |= BUTTON_TALK;
	}

	// allow the game to know if any key at all is
	// currently pressed, even if it isn't bound to anything
	/*
	if ( kg.anykeydown && !cls.keyCatchers ) {
		cmd->buttons |= BUTTON_ANY;
	}
	*/
}


/*
==============
CL_FinishMove
==============
*/
void CL_FinishMove( usercmd_t *cmd ) {
	int		i;

	// copy the state that the cgame is currently sending
	cmd->weapon = cl.cgameUserCmdValue;

#if !defined(STEFX_ELITE_FORCE_SP)
	if (cl.gcmdSendValue)
	{
		cmd->generic_cmd = cl.gcmdValue;
		cl.gcmdSendValue = qfalse;
	}
	else
	{
		cmd->generic_cmd = 0;
	}
#else
	cl.gcmdSendValue = qfalse;
#endif

	// send the current server time so the amount of movement
	// can be determined without allowing cheating
	cmd->serverTime = cl.serverTime;

	for (i=0 ; i<3 ; i++) {
		cmd->angles[i] = ANGLE2SHORT(cl.viewangles[i]);
	}
}

/*
=================
CL_CreateCmd
=================
*/
vec3_t cl_overriddenAngles = {0,0,0};
qboolean cl_overrideAngles = qfalse;
usercmd_t CL_CreateCmd( void ) {
	usercmd_t	cmd;
	vec3_t		oldAngles;

#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	STEFX_SeedClientViewAnglesIfInvalid();
#endif
	VectorCopy( cl.viewangles, oldAngles );

	// keyboard angle adjustment
//	CL_AdjustAngles ();
	
	memset( &cmd, 0, sizeof( cmd ) );

	CL_CmdButtons( &cmd );

	// get basic movement from keyboard
	CL_KeyMove (&cmd);

#ifdef XBOX_DEMO
	static int lastStickData = cl.mouseDx[cl.mouseIndex] + cl.mouseDy[cl.mouseIndex] +
							   cl.joystickAxis[AXIS_SIDE] + cl.joystickAxis[AXIS_FORWARD];
	int newStickData = cl.mouseDx[cl.mouseIndex] + cl.mouseDy[cl.mouseIndex] +
					   cl.joystickAxis[AXIS_SIDE] + cl.joystickAxis[AXIS_FORWARD];
	if( lastStickData != newStickData )
	{
		extern void Demo_TimerKeypress( void );
		Demo_TimerKeypress();
	}
	lastStickData = newStickData;
#endif

	// get basic movement from mouse
	CL_MouseMove( &cmd );

	// get basic movement from joystick
	CL_JoystickMove( &cmd );

	// check to make sure the angles haven't wrapped
	if ( cl.viewangles[PITCH] - oldAngles[PITCH] > 90 ) {
		cl.viewangles[PITCH] = oldAngles[PITCH] + 90;
	} else if ( oldAngles[PITCH] - cl.viewangles[PITCH] > 90 ) {
		cl.viewangles[PITCH] = oldAngles[PITCH] - 90;
	} 

	if ( cl_overrideAngles )
	{
		VectorCopy( cl_overriddenAngles, cl.viewangles );
		cl_overrideAngles = qfalse;
	}
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	STEFX_SeedClientViewAnglesIfInvalid();
#endif
	// store out the final values
	CL_FinishMove( &cmd );

#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	STEFX_ApplySmokeInput( &cmd );

	{
		static int s_cmdLogBudget = 80;
		int logButtons = cmd.buttons & ~BUTTON_WALKING;
		qboolean interestingCmd =
			(cmd.forwardmove != 0) ||
			(cmd.rightmove != 0) ||
			(cmd.upmove != 0) ||
			(logButtons != 0) ||
			((int)oldAngles[YAW] != (int)cl.viewangles[YAW]) ||
			((int)oldAngles[PITCH] != (int)cl.viewangles[PITCH]);

		if ( s_cmdLogBudget > 0 && ( interestingCmd || s_cmdLogBudget > 72 ) )
		{
			Com_PrintfAlways("STEFX: CL_CreateCmd state=%d serverTime=%d move=(%d,%d,%d) buttons=0x%x weapon=%d joy=(%d,%d,%d) view=(%g,%g,%g) old=(%g,%g,%g)\n",
				(int)cls.state,
				cmd.serverTime,
				cmd.forwardmove,
				cmd.rightmove,
				cmd.upmove,
				cmd.buttons,
				cmd.weapon,
				cl.joystickAxis[AXIS_SIDE],
				cl.joystickAxis[AXIS_FORWARD],
				cl.joystickAxis[AXIS_UP],
				cl.viewangles[0],
				cl.viewangles[1],
				cl.viewangles[2],
				oldAngles[0],
				oldAngles[1],
				oldAngles[2]);
			s_cmdLogBudget--;
		}
	}
#endif

	// draw debug graphs of turning for mouse testing
#ifndef _XBOX
	if ( cl_debugMove->integer ) {
		if ( cl_debugMove->integer == 1 ) {
			SCR_DebugGraph( abs(cl.viewangles[YAW] - oldAngles[YAW]), 0 );
		}
		if ( cl_debugMove->integer == 2 ) {
			SCR_DebugGraph( abs(cl.viewangles[PITCH] - oldAngles[PITCH]), 0 );
		}
	}
#endif

	return cmd;
}


/*
=================
CL_CreateNewCommands

Create a new usercmd_t structure for this frame
=================
*/
void CL_CreateNewCommands( void ) {
	usercmd_t	*cmd;
	int			cmdNum;

	// no need to create usercmds until we have a gamestate
//	if ( cls.state < CA_PRIMED ) {
//		return;
//	}

	frame_msec = com_frameTime - old_com_frameTime;

	// if running less than 5fps, truncate the extra time to prevent
	// unexpected moves after a hitch
	if ( frame_msec > 200 ) {
		frame_msec = 200;
	}
	old_com_frameTime = com_frameTime;


	// generate a command for this frame
	cl.cmdNumber++;
	cmdNum = cl.cmdNumber & CMD_MASK;
	cl.cmds[cmdNum] = CL_CreateCmd ();
	cmd = &cl.cmds[cmdNum];
}

/*
=================
CL_ReadyToSendPacket

Returns qfalse if we are over the maxpackets limit
and should choke back the bandwidth a bit by not sending
a packet this frame.  All the commands will still get
delivered in the next packet, but saving a header and
getting more delta compression will reduce total bandwidth.
=================
*/
qboolean CL_ReadyToSendPacket( void ) {
	// don't send anything if playing back a demo
//	if ( cls.state == CA_CINEMATIC ) 
	if ( cls.state == CA_CINEMATIC || CL_IsRunningInGameCinematic())
	{
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
		static int s_stefxReadyCinemaBudget = 12;
		if ( s_stefxReadyCinemaBudget > 0 )
		{
			Com_PrintfAlways( "STEFX: CL_ReadyToSendPacket blocked cinematic state=%d inGameCin=%d cmd=%d realtime=%d\n",
				(int)cls.state,
				CL_IsRunningInGameCinematic() ? 1 : 0,
				cl.cmdNumber,
				cls.realtime );
			--s_stefxReadyCinemaBudget;
		}
#endif
		return qfalse;
	}

	// send every frame for loopbacks
	if (clc.netchan.remoteAddress.type == NA_LOOPBACK)
	{
		return qtrue;
	}

	// if we don't have a valid gamestate yet, only send
	// one packet a second
	if ( cls.state != CA_ACTIVE && cls.state != CA_PRIMED
		&& cls.realtime - clc.lastPacketSentTime < 1000 ) {
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
		static int s_stefxReadyThrottleBudget = 12;
		if ( s_stefxReadyThrottleBudget > 0 )
		{
			Com_PrintfAlways( "STEFX: CL_ReadyToSendPacket throttled state=%d remoteType=%d cmd=%d realtime=%d lastSent=%d\n",
				(int)cls.state,
				(int)clc.netchan.remoteAddress.type,
				cl.cmdNumber,
				cls.realtime,
				clc.lastPacketSentTime );
			--s_stefxReadyThrottleBudget;
		}
#endif
		return qfalse;
	}

	return qtrue;
}

/*
===================
CL_WritePacket

Create and send the command packet to the server
Including both the reliable commands and the usercmds

During normal gameplay, a client packet will contain something like:

4	sequence number
2	qport
4	serverid
4	acknowledged sequence number
4	clc.serverCommandSequence
<optional reliable commands>
1	clc_move or clc_moveNoDelta
1	command count
<count * usercmds>

===================
*/
void CL_WritePacket( void ) {
	msg_t		buf;
	byte		data[MAX_MSGLEN];
	int			i, j;
	usercmd_t	*cmd, *oldcmd;
	usercmd_t	nullcmd;
	int			packetNum;
	int			oldPacketNum;
	int			count;

	// don't send anything if playing back a demo
//	if ( cls.state == CA_CINEMATIC ) 
	if ( cls.state == CA_CINEMATIC || CL_IsRunningInGameCinematic())
	{
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
		static int s_stefxWriteCinemaBudget = 8;
		if ( s_stefxWriteCinemaBudget > 0 )
		{
			Com_PrintfAlways( "STEFX: CL_WritePacket skipped cinematic state=%d inGameCin=%d cmd=%d realtime=%d\n",
				(int)cls.state,
				CL_IsRunningInGameCinematic() ? 1 : 0,
				cl.cmdNumber,
				cls.realtime );
			--s_stefxWriteCinemaBudget;
		}
#endif
		return;
	}

	MSG_Init( &buf, data, sizeof(data) );

	// write any unacknowledged clientCommands
	for ( i = clc.reliableAcknowledge + 1 ; i <= clc.reliableSequence ; i++ ) {
		MSG_WriteByte( &buf, clc_clientCommand );
		MSG_WriteLong( &buf, i );
		MSG_WriteString( &buf, clc.reliableCommands[ i & (MAX_RELIABLE_COMMANDS-1) ] );
	}

	// we want to send all the usercmds that were generated in the last
	// few packet, so even if a couple packets are dropped in a row,
	// all the cmds will make it to the server
	if ( cl_packetdup->integer < 0 ) {
		Cvar_Set( "cl_packetdup", "0" );
	} else if ( cl_packetdup->integer > 5 ) {
		Cvar_Set( "cl_packetdup", "5" );
	}
	oldPacketNum = (clc.netchan.outgoingSequence - 1 - cl_packetdup->integer) & PACKET_MASK;
	count = cl.cmdNumber - cl.packetCmdNumber[ oldPacketNum ];
	if ( count > MAX_PACKET_USERCMDS ) {
		count = MAX_PACKET_USERCMDS;
		Com_Printf("MAX_PACKET_USERCMDS\n");
	}
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	{
		static int s_stefxWritePacketBudget = 64;
		if ( s_stefxWritePacketBudget > 0 && ( count > 0 || s_stefxWritePacketBudget > 56 ) )
		{
			usercmd_t *lastcmd = &cl.cmds[cl.cmdNumber & CMD_MASK];
			Com_PrintfAlways( "STEFX: CL_WritePacket state=%d remoteType=%d cmd=%d oldPacket=%d count=%d serverId=%d lastMove=(%d,%d,%d) lastButtons=0x%x lastWeapon=%d realtime=%d\n",
				(int)cls.state,
				(int)clc.netchan.remoteAddress.type,
				cl.cmdNumber,
				oldPacketNum,
				count,
				cl.serverId,
				lastcmd->forwardmove,
				lastcmd->rightmove,
				lastcmd->upmove,
				lastcmd->buttons,
				lastcmd->weapon,
				cls.realtime );
			--s_stefxWritePacketBudget;
		}
	}
#endif
	if ( count >= 1 ) {
		// begin a client move command
		MSG_WriteByte (&buf, clc_move);

		// write the last reliable message we received
		MSG_WriteLong( &buf, clc.serverCommandSequence );

		// write the current serverId so the server
		// can tell if this is from the current gameState
		MSG_WriteLong (&buf, cl.serverId);

		// write the current time
		MSG_WriteLong (&buf, cls.realtime);

		// let the server know what the last messagenum we
		// got was, so the next message can be delta compressed
		// FIXME: this could just be a bit flag, with the message implicit
		// from the unreliable ack of the netchan
		if (cl_nodelta->integer || !cl.frame.valid) {
			MSG_WriteLong (&buf, -1);	// no compression
		} else {
			MSG_WriteLong (&buf, cl.frame.messageNum);
		}

		// write the cmdNumber so the server can determine which ones it
		// has already received
		MSG_WriteLong( &buf, cl.cmdNumber );

		// write the command count
		MSG_WriteByte( &buf, count );

		// write all the commands, including the predicted command
		memset( &nullcmd, 0, sizeof(nullcmd) );
		oldcmd = &nullcmd;
		for ( i = 0 ; i < count ; i++ ) {
			j = (cl.cmdNumber - count + i + 1) & CMD_MASK;
			cmd = &cl.cmds[j];
			MSG_WriteDeltaUsercmd (&buf, oldcmd, cmd);
			oldcmd = cmd;
		}
	}

	//
	// deliver the message
	//
	packetNum = clc.netchan.outgoingSequence & PACKET_MASK;
	cl.packetTime[ packetNum ] = cls.realtime;
	cl.packetCmdNumber[ packetNum ] = cl.cmdNumber;
	clc.lastPacketSentTime = cls.realtime;
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	{
		static int s_stefxTransmitBudget = 64;
		if ( s_stefxTransmitBudget > 0 && ( count > 0 || s_stefxTransmitBudget > 56 ) )
		{
			Com_PrintfAlways( "STEFX: CL_WritePacket transmit size=%d seq=%d packet=%d count=%d remoteType=%d qport=%d\n",
				buf.cursize,
				clc.netchan.outgoingSequence,
				packetNum,
				count,
				(int)clc.netchan.remoteAddress.type,
				clc.netchan.qport );
			--s_stefxTransmitBudget;
		}
	}
#endif
	Netchan_Transmit (&clc.netchan, buf.cursize, buf.data);	
}

/*
=================
CL_SendCmd

Called every frame to builds and sends a command packet to the server.
=================
*/
void CL_SendCmd( void ) {
	// don't send any message if not connected
	if ( cls.state < CA_CONNECTED ) {
		return;
	} 

	// don't send commands if paused
	if ( com_sv_running->integer && sv_paused->integer && cl_paused->integer ) {
		return;
	}

	// we create commands even if a demo is playing,
	CL_CreateNewCommands();

	// don't send a packet if the last packet was sent too recently
	if ( !CL_ReadyToSendPacket() ) {
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
		static int s_stefxSendBlockedBudget = 32;
		if ( s_stefxSendBlockedBudget > 0 )
		{
			Com_PrintfAlways( "STEFX: CL_SendCmd no packet state=%d cmd=%d remoteType=%d realtime=%d serverTime=%d\n",
				(int)cls.state,
				cl.cmdNumber,
				(int)clc.netchan.remoteAddress.type,
				cls.realtime,
				cl.serverTime );
			--s_stefxSendBlockedBudget;
		}
#endif
		return;
	}

#ifdef _XBOX
	{
		static int s_xboxSendLogs = 0;
		if (s_xboxSendLogs < 16 || cls.state < CA_LOADING)
		{
			Com_PrintfAlways("JA: CL_SendCmd write state=%d cmd=%d serverId=%d remoteType=%d realtime=%d lastSent=%d\n",
				(int)cls.state, cl.cmdNumber, cl.serverId,
				(int)clc.netchan.remoteAddress.type, cls.realtime, clc.lastPacketSentTime);
			++s_xboxSendLogs;
		}
	}
#endif
	CL_WritePacket();
}


/*
============
CL_InitInput
============
*/
void CL_InitInput( void ) {
	Cmd_AddCommand ("centerview",IN_CenterView);

	Cmd_AddCommand ("+moveup",IN_UpDown);
	Cmd_AddCommand ("-moveup",IN_UpUp);
	Cmd_AddCommand ("+movedown",IN_DownDown);
	Cmd_AddCommand ("-movedown",IN_DownUp);
	Cmd_AddCommand ("+left",IN_LeftDown);
	Cmd_AddCommand ("-left",IN_LeftUp);
	Cmd_AddCommand ("+right",IN_RightDown);
	Cmd_AddCommand ("-right",IN_RightUp);
	Cmd_AddCommand ("+forward",IN_ForwardDown);
	Cmd_AddCommand ("-forward",IN_ForwardUp);
	Cmd_AddCommand ("+back",IN_BackDown);
	Cmd_AddCommand ("-back",IN_BackUp);
	Cmd_AddCommand ("+lookup", IN_LookupDown);
	Cmd_AddCommand ("-lookup", IN_LookupUp);
	Cmd_AddCommand ("+lookdown", IN_LookdownDown);
	Cmd_AddCommand ("-lookdown", IN_LookdownUp);
	Cmd_AddCommand ("+strafe", IN_StrafeDown);
	Cmd_AddCommand ("-strafe", IN_StrafeUp);
	Cmd_AddCommand ("+moveleft", IN_MoveleftDown);
	Cmd_AddCommand ("-moveleft", IN_MoveleftUp);
	Cmd_AddCommand ("+moveright", IN_MoverightDown);
	Cmd_AddCommand ("-moveright", IN_MoverightUp);
	Cmd_AddCommand ("+speed", IN_SpeedDown);
	Cmd_AddCommand ("-speed", IN_SpeedUp);
	//xbox hot swappable buttons
#ifdef _XBOX
	Cmd_AddCommand ("+hotswap1", IN_HotSwap1On);
	Cmd_AddCommand ("+hotswap2", IN_HotSwap2On);
	Cmd_AddCommand ("+hotswap3", IN_HotSwap3On);
	Cmd_AddCommand ("-hotswap1", IN_HotSwap1Off);
	Cmd_AddCommand ("-hotswap2", IN_HotSwap2Off);
	Cmd_AddCommand ("-hotswap3", IN_HotSwap3Off);
#endif
	Cmd_AddCommand ("useGivenForce", IN_UseGivenForce);
	//buttons
	Cmd_AddCommand ("+attack", IN_Button0Down);//attack
	Cmd_AddCommand ("-attack", IN_Button0Up);
	Cmd_AddCommand ("+force_lightning", IN_Button1Down);//force lightning
	Cmd_AddCommand ("-force_lightning", IN_Button1Up);
	Cmd_AddCommand ("+useforce", IN_Button2Down);	//use current force power
	Cmd_AddCommand ("-useforce", IN_Button2Up);
	Cmd_AddCommand ("+button2", IN_Button2Down);
	Cmd_AddCommand ("-button2", IN_Button2Up);
	Cmd_AddCommand ("+force_drain", IN_Button3Down);//force drain
	Cmd_AddCommand ("-force_drain", IN_Button3Up);
	Cmd_AddCommand ("+button3", IN_Button3Down);
	Cmd_AddCommand ("-button3", IN_Button3Up);
	Cmd_AddCommand ("+walk", IN_Button4Down);//walking
	Cmd_AddCommand ("-walk", IN_Button4Up);
	Cmd_AddCommand ("+use", IN_Button5Down);//use object
	Cmd_AddCommand ("-use", IN_Button5Up);
	Cmd_AddCommand ("+force_grip", IN_Button6Down);//force jump
	Cmd_AddCommand ("-force_grip", IN_Button6Up);
	Cmd_AddCommand ("+altattack", IN_Button7Down);//altattack
	Cmd_AddCommand ("-altattack", IN_Button7Up);
	Cmd_AddCommand ("+forcefocus", IN_Button8Down);//special saber attacks
	Cmd_AddCommand ("-forcefocus", IN_Button8Up);
	//end buttons
	Cmd_AddCommand ("+mlook", IN_MLookDown);
	Cmd_AddCommand ("-mlook", IN_MLookUp);

	cl_nodelta = Cvar_Get ("cl_nodelta", "0", 0);
	cl_debugMove = Cvar_Get ("cl_debugMove", "0", 0);
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	STEFX_InstallDefaultXboxBindings();
	STEFX_InitSmokeInputCvars();
#endif
}

