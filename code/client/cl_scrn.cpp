// cl_scrn.c -- master for refresh, status bar, console, chat, notify, etc

// leave this as first line for PCH reasons...
//
#include "../server/exe_headers.h"


#include "client.h"
#include "client_ui.h"
#ifdef _XBOX
#include "../win32/xb_log.h"
extern "C" volatile unsigned int g_SPXBPerfScreenDrawMsec;
extern "C" volatile unsigned int g_SPXBPerfEndFrameMsec;
extern "C" volatile unsigned int g_SPXBPerfFrontendMsec;
extern "C" volatile unsigned int g_SPXBPerfBackendMsec;
extern "C" volatile unsigned int g_SPXBPhaseLast;

// Temporary retail-hardware qualification overlay. Keep this in the shared
// client screen path so SP, co-op, and Holomatch measure the same presented
// frame cadence.
#define STEFX_TEMP_HARDWARE_FPS_COUNTER 1
#endif

extern console_t con;

qboolean	scr_initialized;		// ready to draw

cvar_t		*cl_timegraph;
cvar_t		*cl_debuggraph;
cvar_t		*cl_graphheight;
cvar_t		*cl_graphscale;
cvar_t		*cl_graphshift;

/*
================
SCR_DrawNamedPic

Coordinates are 640*480 virtual values
=================
*/
void SCR_DrawNamedPic( float x, float y, float width, float height, const char *picname ) {
	qhandle_t	hShader;

	assert( width != 0 );

	hShader = re.RegisterShader( picname );
	re.DrawStretchPic( x, y, width, height, 0, 0, 1, 1, hShader );
}


/*
================
SCR_FillRect

Coordinates are 640*480 virtual values
=================
*/
void SCR_FillRect( float x, float y, float width, float height, const float *color ) {
	re.SetColor( color );

	re.DrawStretchPic( x, y, width, height, 0, 0, 0, 0, cls.whiteShader );

	re.SetColor( NULL );
}


/*
================
SCR_DrawPic

Coordinates are 640*480 virtual values
A width of 0 will draw with the original image width
=================
*/
void SCR_DrawPic( float x, float y, float width, float height, qhandle_t hShader ) {
	re.DrawStretchPic( x, y, width, height, 0, 0, 1, 1, hShader );
}


/*
** SCR_DrawBigChar
** big chars are drawn at 640*480 virtual screen size
*/
void SCR_DrawBigChar( int x, int y, int ch ) {
	int row, col;
	float frow, fcol;
	float size;
	float	ax, ay, aw, ah;

	ch &= 255;

	if ( ch == ' ' ) {
		return;
	}

	if ( y < -BIGCHAR_HEIGHT ) {
		return;
	}

	ax = x;
	ay = y;
	aw = BIGCHAR_WIDTH;
	ah = BIGCHAR_HEIGHT;

	row = ch>>4;
	col = ch&15;

	frow = row*0.0625;
	fcol = col*0.0625;
	size = 0.0625;
/*
	re.DrawStretchPic( ax, ay, aw, ah,
					   fcol, frow, 
					   fcol + size, frow + size, 
					   cls.charSetShader );
*/
	float size2;

	frow = row*0.0625;
	fcol = col*0.0625;
	size = 0.03125;
	size2 = 0.0625;

	re.DrawStretchPic( ax, ay, aw, ah,
					   fcol, frow, 
					   fcol + size, frow + size2, 
					   cls.charSetShader );

}

/*
** SCR_DrawSmallChar
** small chars are drawn at native screen resolution
*/
void SCR_DrawSmallChar( int x, int y, int ch ) {
	int row, col;
	float frow, fcol;
	float size;

	ch &= 255;

	if ( ch == ' ' ) {
		return;
	}

	if ( y < -SMALLCHAR_HEIGHT ) {
		return;
	}

	row = ch>>4;
	col = ch&15;
/*
	frow = row*0.0625;
	fcol = col*0.0625;
	size = 0.0625;

	re.DrawStretchPic( x, y, SMALLCHAR_WIDTH, SMALLCHAR_HEIGHT,
					   fcol, frow, 
					   fcol + size, frow + size, 
					   cls.charSetShader );
*/

	float size2;

	frow = row*0.0625;
	fcol = col*0.0625;
	size = 0.03125;
	size2 = 0.0625;

	re.DrawStretchPic( x * con.xadjust, y * con.yadjust, 
						SMALLCHAR_WIDTH * con.xadjust, SMALLCHAR_HEIGHT * con.yadjust, 
		fcol, frow, 
		fcol + size, frow + size2, 
		cls.charSetShader );

}



/*
==================
SCR_DrawBigString[Color]

Draws a multi-colored string with a drop shadow, optionally forcing
to a fixed color.

Coordinates are at 640 by 480 virtual resolution
==================
*/
void SCR_DrawBigStringExt( int x, int y, const char *string, float *setColor, qboolean forceColor ) {
	vec4_t		color;
	const char	*s;
	int			xx;

	// draw the drop shadow
	color[0] = color[1] = color[2] = 0;
	color[3] = setColor[3];
	re.SetColor( color );
	s = string;
	xx = x;
	while ( *s ) {
		if ( Q_IsColorString( s ) ) {
			s += 2;
			continue;
		}
		SCR_DrawBigChar( xx+2, y+2, *s );
		xx+=16;
		s++;
	}


	// draw the colored text
	s = string;
	xx = x;
	re.SetColor( setColor );
	while ( *s ) {
		if ( Q_IsColorString( s ) ) {
			if ( !forceColor ) {
				memcpy( color, g_color_table[ColorIndex(*(s+1))], sizeof( color ) );
				color[3] = setColor[3];
				re.SetColor( color );
			}
			s += 2;
			continue;
		}
		SCR_DrawBigChar( xx, y, *s );
		xx+=16;
		s++;
	}
	re.SetColor( NULL );
}


void SCR_DrawBigString( int x, int y, const char *s, float alpha ) {
	float	color[4];

	color[0] = color[1] = color[2] = 1.0;
	color[3] = alpha;
	SCR_DrawBigStringExt( x, y, s, color, qfalse );
}

void SCR_DrawBigStringColor( int x, int y, const char *s, vec4_t color ) {
	SCR_DrawBigStringExt( x, y, s, color, qtrue );
}

#if defined(_XBOX) && STEFX_TEMP_HARDWARE_FPS_COUNTER
static void SCR_DrawHardwareFPS( void )
{
	static int sampleStart;
	static int sampleFrames;
	static unsigned int fpsTenths;
	static char text[16] = "FPS --.-";
	const int now = Sys_Milliseconds();
	int elapsed;
	int i;
	vec4_t background = { 0.0f, 0.0f, 0.0f, 0.70f };
	vec4_t shadow = { 0.0f, 0.0f, 0.0f, 1.0f };
	vec4_t color = { 1.0f, 1.0f, 1.0f, 1.0f };

	if (!sampleStart)
	{
		sampleStart = now;
	}

	++sampleFrames;
	elapsed = now - sampleStart;
	if (elapsed >= 1000)
	{
		fpsTenths = (unsigned int)(((unsigned __int64)sampleFrames * 10000) / elapsed);
		Com_sprintf(text, sizeof(text), "FPS %u.%u", fpsTenths / 10, fpsTenths % 10);
		sampleFrames = 0;
		sampleStart = now;
	}

	if (fpsTenths)
	{
		if (fpsTenths < 200)
		{
			color[1] = 0.25f;
			color[2] = 0.25f;
		}
		else if (fpsTenths < 300)
		{
			color[2] = 0.20f;
		}
		else
		{
			color[0] = 0.35f;
			color[2] = 0.35f;
		}
	}

	SCR_FillRect(4, 4, 8 + (int)strlen(text) * SMALLCHAR_WIDTH, SMALLCHAR_HEIGHT + 6, background);

	re.SetColor(shadow);
	for (i = 0; text[i]; ++i)
	{
		SCR_DrawSmallChar(9 + i * SMALLCHAR_WIDTH, 8, text[i]);
	}

	re.SetColor(color);
	for (i = 0; text[i]; ++i)
	{
		SCR_DrawSmallChar(8 + i * SMALLCHAR_WIDTH, 7, text[i]);
	}
	re.SetColor(NULL);
}
#endif


/*
** SCR_Strlen -- skips color escape codes
*/
static int SCR_Strlen( const char *str ) {
	const char *s = str;
	int count = 0;

	while ( *s ) {
		if ( Q_IsColorString( s ) ) {
			s += 2;
		} else {
			count++;
			s++;
		}
	}

	return count;
}

/*
** SCR_GetBigStringWidth
*/ 
int	SCR_GetBigStringWidth( const char *str ) {
	return SCR_Strlen( str ) * 16;
}

//===============================================================================


/*
===============================================================================

DEBUG GRAPH

===============================================================================
*/
#ifndef _XBOX
typedef struct
{
	float	value;
	int		color;
} graphsamp_t;

static	int			current;
static	graphsamp_t	values[1024];

/*
==============
SCR_DebugGraph
==============
*/
void SCR_DebugGraph (float value, int color)
{
	values[current&1023].value = value;
	values[current&1023].color = color;
	current++;
}

/*
==============
SCR_DrawDebugGraph
==============
*/
void SCR_DrawDebugGraph (void)
{
	int		a, x, y, w, i, h;
	float	v;
	int		color;

	//
	// draw the graph
	//
	w = cls.glconfig.vidWidth;
	x = 0;
	y = cls.glconfig.vidHeight;
	re.SetColor( g_color_table[0] );
	re.DrawStretchPic(x, y - cl_graphheight->integer, 
		w, cl_graphheight->integer, 0, 0, 0, 0, 0 );
	re.SetColor( NULL );

	for (a=0 ; a<w ; a++)
	{
		i = (current-1-a+1024) & 1023;
		v = values[i].value;
		color = values[i].color;
		v = v * cl_graphscale->integer + cl_graphshift->integer;
		
		if (v < 0)
			v += cl_graphheight->integer * (1+(int)(-v / cl_graphheight->integer));
		h = (int)v % cl_graphheight->integer;
		re.DrawStretchPic( x+w-1-a, y - h, 1, h, 0, 0, 0, 0, 0 );
	}
}
#endif	// _XBOX
//=============================================================================

/*
==================
SCR_Init
==================
*/
void SCR_Init( void ) {
	cl_timegraph = Cvar_Get ("timegraph", "0", CVAR_CHEAT);
	cl_debuggraph = Cvar_Get ("debuggraph", "0", CVAR_CHEAT);
	cl_graphheight = Cvar_Get ("graphheight", "32", CVAR_CHEAT);
	cl_graphscale = Cvar_Get ("graphscale", "1", CVAR_CHEAT);
	cl_graphshift = Cvar_Get ("graphshift", "0", CVAR_CHEAT);

	scr_initialized = qtrue;
}


//=======================================================

void UI_SetActiveMenu( const char* menuname,const char *menuID );
void _UI_Refresh( int realtime );
void UI_DrawConnect( const char *servername, const char * updateInfoString );
#ifdef _XBOX
extern bool g_xboxDirectMapBootQueued;
extern bool Sys_IsDirectMapBoot(void);
#endif

/*
==================
SCR_DrawScreenField

This will be called twice if rendering in stereo mode
==================
*/
void SCR_DrawScreenField( stereoFrame_t stereoFrame ) {
#ifdef _XBOX
	static int s_xboxDrawScreenTraceCount = 0;
	static int s_xboxDrawScreenActiveTraceCount = 0;
	const int xboxTraceScreenLate = (cls.state == CA_ACTIVE && cl.serverTime >= 3600 && cl.serverTime <= 5000);
	const int xboxTraceScreen = (cls.state == CA_ACTIVE)
		? (s_xboxDrawScreenActiveTraceCount < 16 || ((s_xboxDrawScreenActiveTraceCount & 255) == 0))
		: (s_xboxDrawScreenTraceCount < 8);
	if (xboxTraceScreen)
	{
		XBLF("JA: SCR_DrawScreenField enter state=%d serverTime=%d stereo=%d", (int)cls.state, cl.serverTime, (int)stereoFrame);
	}
	if (xboxTraceScreenLate)
	{
		XBLF("JA: CL_EARLY SCR_DrawScreenField enter state=%d frame=%d serverTime=%d stereo=%d",
			(int)cls.state, cls.framecount, cl.serverTime, (int)stereoFrame);
	}
#endif

	#ifdef _XBOX
	if (xboxTraceScreenLate) XBLog_Write("JA: CL_EARLY SCR_DrawScreenField before re.BeginFrame");
	if (xboxTraceScreen) XBLog_Write("JA: SCR_DrawScreenField: re.BeginFrame...");
	#endif
#ifdef _XBOX
	g_SPXBPhaseLast = 0x53464230; /* 'SFB0' */
#endif
	re.BeginFrame( stereoFrame );
#ifdef _XBOX
	g_SPXBPhaseLast = 0x53464231; /* 'SFB1' */
#endif
#ifdef _XBOX
	if (xboxTraceScreenLate) XBLog_Write("JA: CL_EARLY SCR_DrawScreenField after re.BeginFrame");
	if (xboxTraceScreen) XBLog_Write("JA: SCR_DrawScreenField: re.BeginFrame done");
#endif

	// wide aspect ratio screens need to have the sides cleared
	// unless they are displaying game renderings
#ifndef _XBOX
	// Xbox no want this
	if ( cls.state != CA_ACTIVE ) {
		if ( cls.glconfig.vidWidth * 480 > cls.glconfig.vidHeight * 640 ) {
			re.SetColor( g_color_table[0] );
			re.DrawStretchPic( 0, 0, cls.glconfig.vidWidth, cls.glconfig.vidHeight, 0, 0, 0, 0, 0 );
			re.SetColor( NULL );
		}
	}
#endif

	// if the menu is going to cover the entire screen, we
	// don't need to render anything under it
#ifdef _XBOX
	const qboolean xboxUiCatcherActive = (qboolean)((cls.keyCatchers & KEYCATCH_UI) != 0);
	const qboolean stefxObjectivesOverlayClientActive = CL_STEFX_ObjectivesOverlayActive();
	const qboolean stefxMissionFailedOverlayClientActive = CL_STEFX_MissionFailedOverlayActive();
	const qboolean xboxForceDirectMapGameDraw =
		(Sys_IsDirectMapBoot() && cls.state == CA_ACTIVE && !xboxUiCatcherActive && !stefxObjectivesOverlayClientActive && !stefxMissionFailedOverlayClientActive);
	const qboolean stefxObjectivesOverlayActive =
		(qboolean)(stefxObjectivesOverlayClientActive || Cvar_VariableIntegerValue("stefx_objectivesOverlay") != 0);
	static int s_stefxObjectivesScreenTraceBudget = 16;
#else
	const qboolean xboxForceDirectMapGameDraw = qfalse;
	const qboolean stefxObjectivesOverlayActive = qfalse;
#endif
#ifdef _XBOX
	if ((stefxObjectivesOverlayActive || xboxUiCatcherActive) && s_stefxObjectivesScreenTraceBudget > 0)
	{
		XBLog_Write(va("STEFX: SCR objective overlay decision state=%d serverTime=%d uiStarted=%d catcher=0x%x overlay='%s' paused='%s' fullscreen=%d forceMap=%d",
			(int)cls.state,
			cl.serverTime,
			(int)cls.uiStarted,
			(unsigned int)cls.keyCatchers,
			Cvar_VariableString("stefx_objectivesOverlay"),
			Cvar_VariableString("cl_paused"),
			_UI_IsFullscreen() ? 1 : 0,
			xboxForceDirectMapGameDraw ? 1 : 0));
		--s_stefxObjectivesScreenTraceBudget;
	}
#endif
#ifdef _XBOX
	static int stefxCinematicFullscreenBypassLogBudget = 4;
	const qboolean stefxCinematicNeedsDraw =
		(cls.state == CA_CINEMATIC || CL_IsRunningInGameCinematic() || CL_InGameCinematicOnStandBy());
	if (stefxCinematicNeedsDraw && _UI_IsFullscreen() && stefxCinematicFullscreenBypassLogBudget > 0)
	{
		XBLF("STEFX: SCR_DrawScreenField drawing cinematic despite fullscreen UI state=%d catcher=0x%x",
			(int)cls.state, (unsigned int)cls.keyCatchers);
		--stefxCinematicFullscreenBypassLogBudget;
	}
	{
		static int s_stefxCinDecisionBudget = 24;
		if (stefxCinematicNeedsDraw && s_stefxCinDecisionBudget > 0)
		{
			XBLF("STEFX_CIN_DRAW: screen decision state=%d uiStarted=%d catcher=0x%x fullscreen=%d needs=%d forceMap=%d",
				(int)cls.state,
				(int)cls.uiStarted,
				(unsigned int)cls.keyCatchers,
				_UI_IsFullscreen() ? 1 : 0,
				stefxCinematicNeedsDraw ? 1 : 0,
				xboxForceDirectMapGameDraw ? 1 : 0);
			--s_stefxCinDecisionBudget;
		}
	}
#else
	const qboolean stefxCinematicNeedsDraw = qfalse;
#endif
	if ( stefxCinematicNeedsDraw || xboxForceDirectMapGameDraw || !_UI_IsFullscreen() ) {
		switch( cls.state ) {
		default:
			Com_Error( ERR_FATAL, "SCR_DrawScreenField: bad cls.state" );
			break;
		case CA_CINEMATIC:
			SCR_DrawCinematic();
			break;
		case CA_DISCONNECTED:
			// force menu up
			if (!(cls.keyCatchers & KEYCATCH_UI))
			{
				UI_SetActiveMenu( "mainMenu",NULL );	//			VM_Call( uivm, UI_SET_ACTIVE_MENU, UIMENU_MAIN );
			}
			break;
		case CA_CONNECTING:
		case CA_CHALLENGING:
		case CA_CONNECTED:
			// connecting clients will only show the connection dialog
			UI_DrawConnect( clc.servername, cls.updateInfoString );
			break;
		case CA_LOADING:
		case CA_PRIMED:
			// We got past the time when the UI needs to prevent swapping
			extern bool connectSwapOverride;
			connectSwapOverride = false;

			// draw the game information screen and loading progress
			CL_CGameRendering( stereoFrame );
			break;
		case CA_ACTIVE:
			if (CL_IsRunningInGameCinematic() || CL_InGameCinematicOnStandBy())
			{
				SCR_DrawCinematic();				
			}
			else
			{
				#ifdef _XBOX
				if (xboxTraceScreenLate) XBLog_Write("JA: CL_EARLY SCR_DrawScreenField before CL_CGameRendering");
				if (xboxTraceScreen) XBLog_Write("JA: SCR_DrawScreenField: CL_CGameRendering...");
				#endif
				g_SPXBPhaseLast = 0x53464330; /* 'SFC0' */
				CL_CGameRendering( stereoFrame );
				g_SPXBPhaseLast = 0x53464331; /* 'SFC1' */
				#ifdef _XBOX
				if (xboxTraceScreenLate) XBLog_Write("JA: CL_EARLY SCR_DrawScreenField after CL_CGameRendering");
				if (xboxTraceScreen) XBLog_Write("JA: SCR_DrawScreenField: CL_CGameRendering done");
				#endif
			}
			break;
		}
	}

#ifndef _XBOX // on xbox this is rendered right before a flip
	re.ProcessDissolve();
#endif // _XBOX

	// draw downloading progress bar

	// the menu draws next
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	if ( stefxObjectivesOverlayClientActive )
	{
		CL_STEFX_DrawObjectivesOverlay();
		return;
	}
	if ( stefxMissionFailedOverlayClientActive )
	{
		CL_STEFX_DrawMissionFailedOverlay();
		return;
	}
#endif
#ifdef _XBOX
	if (xboxForceDirectMapGameDraw) {
		if (stefxObjectivesOverlayActive)
		{
			XBLog_Write("STEFX: SCR objective overlay blocked by direct-map return");
		}
		if (xboxTraceScreenLate) XBLog_Write("JA: CL_EARLY SCR_DrawScreenField direct-map return before UI");
		if (xboxTraceScreen) XBLog_Write("JA: SCR_DrawScreenField: skipping UI refresh for direct-map active frame");
		return;
	}
#endif
	#ifdef _XBOX
	if (stefxObjectivesOverlayActive)
	{
		XBLog_Write("STEFX: SCR objective overlay calling _UI_Refresh");
	}
	if (xboxTraceScreen) XBLog_Write("JA: SCR_DrawScreenField: _UI_Refresh...");
	#endif
#ifdef _XBOX
	g_SPXBPhaseLast = 0x53465530; /* 'SFU0' */
#endif
	_UI_Refresh( cls.realtime );
#ifdef _XBOX
	g_SPXBPhaseLast = 0x53465531; /* 'SFU1' */
#endif
#ifdef _XBOX
	if (xboxTraceScreen)
	{
		XBLog_Write("JA: SCR_DrawScreenField: _UI_Refresh done");
		XBLog_Write("JA: SCR_DrawScreenField done");
		if (cls.state == CA_ACTIVE)
		{
			s_xboxDrawScreenActiveTraceCount++;
		}
		else
		{
			s_xboxDrawScreenTraceCount++;
		}
	}
#endif

	// console draws next
//	Con_DrawConsole ();

	// debug graph can be drawn on top of anything
#ifndef _XBOX
	if ( cl_debuggraph->integer || cl_timegraph->integer ) {
		SCR_DrawDebugGraph ();
	}
#endif
}

/*
==================
SCR_UpdateScreen

This is called every frame, and can also be called explicitly to flush
text to the screen.
==================
*/
void SCR_UpdateScreen( void ) {
	static int	recursive;
#ifdef _XBOX
	int xboxScreenPhaseStart;
#endif

	if ( !scr_initialized ) {
		return;				// not initialized yet
	}

#ifdef _XBOX
	static int s_xboxUpdateScreenTraceCount = 0;
	static int s_xboxUpdateScreenActiveTraceCount = 0;
	const int xboxTraceScreenLate = (cls.state == CA_ACTIVE && cl.serverTime >= 3600 && cl.serverTime <= 4600);
	const int xboxTraceScreenTight = xboxTraceScreenLate;
	const int xboxTraceScreen = (cls.state == CA_ACTIVE)
		? (s_xboxUpdateScreenActiveTraceCount < 16 || ((s_xboxUpdateScreenActiveTraceCount & 255) == 0))
		: (s_xboxUpdateScreenTraceCount < 8);
	static int s_xboxUpdateScreenCinematicTraceCount = 0;
	if (cls.state == CA_CINEMATIC && s_xboxUpdateScreenCinematicTraceCount < 24)
	{
		XBLF("STEFX_CIN_DRAW: update enter state=%d ui=%d cgame=%d catcher=0x%x recursive=%d",
			(int)cls.state,
			cls.uiStarted ? 1 : 0,
			cls.cgameStarted ? 1 : 0,
			(unsigned int)cls.keyCatchers,
			recursive);
	}
	if (xboxTraceScreenLate)
	{
		XBLF("JA: CL_EARLY SCR_UpdateScreen enter state=%d frame=%d realtime=%d serverTime=%d recursive=%d stereo=%d",
			(int)cls.state, cls.framecount, cls.realtime, cl.serverTime, recursive, cls.glconfig.stereoEnabled);
	}
#endif

	// load the ref / ui / cgame if needed
#ifdef _XBOX
	g_SPXBPhaseLast = 0x53524330; /* 'SRC0' */
#endif
	CL_StartHunkUsers();
#ifdef _XBOX
	g_SPXBPhaseLast = 0x53524331; /* 'SRC1' */
#endif

#ifdef _XBOX
	if (cls.state == CA_CINEMATIC && s_xboxUpdateScreenCinematicTraceCount < 24)
	{
		XBLF("STEFX_CIN_DRAW: update after hunk state=%d ui=%d cgame=%d catcher=0x%x",
			(int)cls.state,
			cls.uiStarted ? 1 : 0,
			cls.cgameStarted ? 1 : 0,
			(unsigned int)cls.keyCatchers);
	}
	if (xboxTraceScreenLate)
	{
		XBLF("JA: CL_EARLY SCR_UpdateScreen after CL_StartHunkUsers state=%d ui=%d cgame=%d",
			(int)cls.state, cls.uiStarted, cls.cgameStarted);
	}
#endif

	if ( ++recursive > 2 ) {
		Com_Error( ERR_FATAL, "SCR_UpdateScreen: recursively called" );
	}
	recursive = qtrue;

#ifdef _XBOX
	if (xboxTraceScreenLate)
	{
		XBLF("JA: CL_EARLY SCR_UpdateScreen recursive set recursive=%d", recursive);
	}
#endif

	// if running in stereo, we need to draw the frame twice
#ifdef _XBOX
	xboxScreenPhaseStart = Sys_Milliseconds();
#endif
	if ( cls.glconfig.stereoEnabled ) {
#ifdef _XBOX
		if (xboxTraceScreenTight) XBLog_Write("JA: SCR_TIGHT draw left");
		if (xboxTraceScreen) XBLog_Write("JA: SCR_UpdateScreen: draw left...");
#endif
		SCR_DrawScreenField( STEREO_LEFT );
#ifdef _XBOX
		if (xboxTraceScreenTight) XBLog_Write("JA: SCR_TIGHT draw right");
		if (xboxTraceScreen) XBLog_Write("JA: SCR_UpdateScreen: draw right...");
#endif
		SCR_DrawScreenField( STEREO_RIGHT );
	} else {
#ifdef _XBOX
		if (xboxTraceScreenLate) XBLog_Write("JA: CL_EARLY SCR_UpdateScreen before draw center");
		if (xboxTraceScreenTight) XBLog_Write("JA: SCR_TIGHT before draw center");
		if (xboxTraceScreen) XBLog_Write("JA: SCR_UpdateScreen: draw center...");
#endif
		g_SPXBPhaseLast = 0x53524332; /* 'SRC2' */
		SCR_DrawScreenField( STEREO_CENTER );
		g_SPXBPhaseLast = 0x53524333; /* 'SRC3' */
	}
#if defined(_XBOX) && STEFX_TEMP_HARDWARE_FPS_COUNTER
	SCR_DrawHardwareFPS();
#endif
#ifdef _XBOX
	g_SPXBPerfScreenDrawMsec = (unsigned int)(Sys_Milliseconds() - xboxScreenPhaseStart);
#endif
#ifdef _XBOX
	if (xboxTraceScreenLate) XBLog_Write("JA: CL_EARLY SCR_UpdateScreen after draw");
	if (xboxTraceScreenTight) XBLog_Write("JA: SCR_TIGHT draw done");
	if (xboxTraceScreen) XBLog_Write("JA: SCR_UpdateScreen: draw done");
#endif

#ifdef _XBOX
	if (xboxTraceScreenLate) XBLog_Write("JA: CL_EARLY SCR_UpdateScreen before re.EndFrame");
	if (xboxTraceScreenTight) XBLog_Write("JA: SCR_TIGHT before re.EndFrame");
	if (xboxTraceScreen) XBLog_Write("JA: SCR_UpdateScreen: re.EndFrame...");
#endif
#ifdef _XBOX
	xboxScreenPhaseStart = Sys_Milliseconds();
#endif
	#ifdef _XBOX
	g_SPXBPhaseLast = 0x53524334; /* 'SRC4' */
	re.EndFrame( &time_frontend, &time_backend );
	g_SPXBPhaseLast = 0x53524335; /* 'SRC5' */
	g_SPXBPerfFrontendMsec = (unsigned int)time_frontend;
	g_SPXBPerfBackendMsec = (unsigned int)time_backend;
	#else
	if ( com_speeds->integer ) {
		re.EndFrame( &time_frontend, &time_backend );
	} else {
		re.EndFrame( NULL, NULL );
	}
	#endif
#ifdef _XBOX
	g_SPXBPerfEndFrameMsec = (unsigned int)(Sys_Milliseconds() - xboxScreenPhaseStart);
#endif
#ifdef _XBOX
	if (xboxTraceScreenLate) XBLog_Write("JA: CL_EARLY SCR_UpdateScreen after re.EndFrame");
	if (xboxTraceScreenTight) XBLog_Write("JA: SCR_TIGHT after re.EndFrame");
	if (xboxTraceScreen)
	{
		XBLog_Write("JA: SCR_UpdateScreen: re.EndFrame done");
		if (cls.state == CA_ACTIVE)
		{
			s_xboxUpdateScreenActiveTraceCount++;
		}
		else
		{
			s_xboxUpdateScreenTraceCount++;
		}
	}
#endif

	recursive = 0;
#ifdef _XBOX
	if (xboxTraceScreenLate) XBLog_Write("JA: CL_EARLY SCR_UpdateScreen before return");
	if (cls.state == CA_CINEMATIC && s_xboxUpdateScreenCinematicTraceCount < 24)
	{
		XBLF("STEFX_CIN_DRAW: update exit state=%d ui=%d cgame=%d catcher=0x%x",
			(int)cls.state,
			cls.uiStarted ? 1 : 0,
			cls.cgameStarted ? 1 : 0,
			(unsigned int)cls.keyCatchers);
		++s_xboxUpdateScreenCinematicTraceCount;
	}
#endif
}

// this stuff is only used by the savegame (SG) code for screenshots...
//
#ifdef _XBOX

/*
static byte	bScreenData[SG_SCR_WIDTH * SG_SCR_HEIGHT * 4];
static qboolean screenDataValid = qfalse;
void SCR_UnprecacheScreenshot()
{
	screenDataValid = qfalse;
}
*/

void SCR_PrecacheScreenshot()
{
	// No screenshots unless connected to single player local server...
	//
//	char *psInfo = cl.gameState.stringData + cl.gameState.stringOffsets[ CS_SERVERINFO ];
//	int iMaxClients = atoi(Info_ValueForKey( psInfo, "sv_maxclients" ));		

	// (no need to check single-player status in voyager, this code base is all singleplayer)
	if ( cls.state != CA_ACTIVE )
	{	
		return;
	}

#ifndef _XBOX
	if (cls.keyCatchers == 0)
	{
		// in-game...
		//
//		SCR_UnprecacheScreenshot();
//		pbScreenData = (byte *)Z_Malloc(SG_SCR_WIDTH * SG_SCR_HEIGHT * 4);		
		S_ClearSoundBuffer();	// clear DMA etc because the following glReadPixels() call can take ages
		re.GetScreenShot( (byte *) &bScreenData, SG_SCR_WIDTH, SG_SCR_HEIGHT);
		screenDataValid = qtrue;
	}
	else
	{
		// we're in the console, or menu, or message input...
		//
	}
#endif

	// save the current screenshot to the user space to be used
	// with a savegame
#ifdef _XBOX
	extern void SaveCompressedScreenshot( void );
	SaveCompressedScreenshot();
#endif

}

/*
byte *SCR_GetScreenshot(qboolean *qValid)
{
	if (!screenDataValid) {
		SCR_PrecacheScreenshot();
	}
	if (qValid) {
		*qValid = screenDataValid;
	}
	return (byte *)&bScreenData;
}

// called from save-game code to set the lo-res loading screen to be the one from the save file...
//
void SCR_SetScreenshot(const byte *pbData, int w, int h)
{
	if (w == SG_SCR_WIDTH && h == SG_SCR_HEIGHT)
	{
		screenDataValid = qtrue;
		memcpy(&bScreenData, pbData, SG_SCR_WIDTH*SG_SCR_HEIGHT*4);
	}
	else
	{
		screenDataValid = qfalse;
		memset(&bScreenData, 0,      SG_SCR_WIDTH*SG_SCR_HEIGHT*4);
	}
}
*/

// This is just a client-side wrapper for the function RE_TempRawImage_ReadFromFile() in the renderer code...
//
/*
byte* SCR_TempRawImage_ReadFromFile(const char *psLocalFilename, int *piWidth, int *piHeight, byte *pbReSampleBuffer, qboolean qbVertFlip)
{
	return re.TempRawImage_ReadFromFile(psLocalFilename, piWidth, piHeight, pbReSampleBuffer, qbVertFlip);
}
//
// ditto (sort of)...
//
void  SCR_TempRawImage_CleanUp()
{
	re.TempRawImage_CleanUp();
}
*/
#endif
