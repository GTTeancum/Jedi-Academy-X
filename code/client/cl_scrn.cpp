// cl_scrn.c -- master for refresh, status bar, console, chat, notify, etc

// leave this as first line for PCH reasons...
//
#include "../server/exe_headers.h"


#include "client.h"
#include "client_ui.h"
#ifdef _XBOX
#include "../win32/xb_log.h"
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
	const int xboxTraceScreen = (cls.state == CA_ACTIVE)
		? (s_xboxDrawScreenActiveTraceCount < 16 || ((s_xboxDrawScreenActiveTraceCount & 255) == 0))
		: (s_xboxDrawScreenTraceCount < 8);
	if (xboxTraceScreen)
	{
		XBLF("JA: SCR_DrawScreenField enter state=%d serverTime=%d stereo=%d", (int)cls.state, cl.serverTime, (int)stereoFrame);
	}
#endif

	#ifdef _XBOX
	if (xboxTraceScreen) XBLog_Write("JA: SCR_DrawScreenField: re.BeginFrame...");
	#endif
	re.BeginFrame( stereoFrame );
#ifdef _XBOX
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
	if ( !_UI_IsFullscreen() ) {
		switch( cls.state ) {
		default:
			Com_Error( ERR_FATAL, "SCR_DrawScreenField: bad cls.state" );
			break;
		case CA_CINEMATIC:
			SCR_DrawCinematic();
			break;
		case CA_DISCONNECTED:
			// force menu up
			UI_SetActiveMenu( "mainMenu",NULL );	//			VM_Call( uivm, UI_SET_ACTIVE_MENU, UIMENU_MAIN );
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
				if (xboxTraceScreen) XBLog_Write("JA: SCR_DrawScreenField: CL_CGameRendering...");
				#endif
				CL_CGameRendering( stereoFrame );
				#ifdef _XBOX
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
	#ifdef _XBOX
	if (xboxTraceScreen) XBLog_Write("JA: SCR_DrawScreenField: _UI_Refresh...");
	#endif
	_UI_Refresh( cls.realtime );
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

	if ( !scr_initialized ) {
		return;				// not initialized yet
	}

	// load the ref / ui / cgame if needed
	CL_StartHunkUsers();

	if ( ++recursive > 2 ) {
		Com_Error( ERR_FATAL, "SCR_UpdateScreen: recursively called" );
	}
	recursive = qtrue;

#ifdef _XBOX
	static int s_xboxUpdateScreenTraceCount = 0;
	static int s_xboxUpdateScreenActiveTraceCount = 0;
	const int xboxTraceScreenTight = (qfalse && cls.state == CA_ACTIVE && cls.realtime >= 35000 && cls.realtime <= 70000);
	const int xboxTraceScreen = (cls.state == CA_ACTIVE)
		? (s_xboxUpdateScreenActiveTraceCount < 16 || ((s_xboxUpdateScreenActiveTraceCount & 255) == 0))
		: (s_xboxUpdateScreenTraceCount < 8);
#endif

	// if running in stereo, we need to draw the frame twice
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
		if (xboxTraceScreenTight) XBLog_Write("JA: SCR_TIGHT before draw center");
		if (xboxTraceScreen) XBLog_Write("JA: SCR_UpdateScreen: draw center...");
#endif
		SCR_DrawScreenField( STEREO_CENTER );
	}
#ifdef _XBOX
	if (xboxTraceScreenTight) XBLog_Write("JA: SCR_TIGHT draw done");
	if (xboxTraceScreen) XBLog_Write("JA: SCR_UpdateScreen: draw done");
#endif

#ifdef _XBOX
	if (xboxTraceScreenTight) XBLog_Write("JA: SCR_TIGHT before re.EndFrame");
	if (xboxTraceScreen) XBLog_Write("JA: SCR_UpdateScreen: re.EndFrame...");
#endif
	if ( com_speeds->integer ) {
		re.EndFrame( &time_frontend, &time_backend );
	} else {
		re.EndFrame( NULL, NULL );
	}
#ifdef _XBOX
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
