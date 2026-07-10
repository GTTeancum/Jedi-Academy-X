/**********************************************************************
	UI_ATOMS.C

	User interface building blocks and support functions.
**********************************************************************/

// leave this at the top of all UI_xxxx files for PCH reasons...
//

#include "../server/exe_headers.h"

#include "ui_local.h"
#include "gameinfo.h"
#include "../qcommon/stv_version.h"
#ifdef _XBOX
#include "../win32/xb_log.h"
#endif

uiimport_t	ui;
uiStatic_t	uis;

//externs
static void UI_LoadMenu_f( void );
static void UI_SaveMenu_f( void );

//locals


/*
=================
UI_ForceMenuOff
=================
*/
void UI_ForceMenuOff (void)
{
#ifdef _XBOX
	XBLF("STEFX: UI_ForceMenuOff catcherBefore=0x%x paused='%s'",
		ui.Key_GetCatcher(),
		UI_Cvar_VariableString("cl_paused"));
#endif
	uis.menusp = 0;
	uis.activemenu = NULL;
	memset(uis.stack, 0, sizeof(uis.stack));
	UI_EFMainMenu_Deactivate();
	UI_EFPauseMenu_Deactivate();
	ui.Key_SetCatcher( ui.Key_GetCatcher() & ~KEYCATCH_UI );
	ui.Key_ClearStates();
	ui.Cvar_Set( "cl_paused", "0" );
#ifdef _XBOX
	XBLF("STEFX: UI_ForceMenuOff done catcherAfter=0x%x paused='%s'",
		ui.Key_GetCatcher(),
		UI_Cvar_VariableString("cl_paused"));
#endif
}


/*
=================
UI_SetActiveMenu - 
	this should be the ONLY way the menu system is brought up
 
=================
*/
extern void S_StopAllSoundsExceptMusic( void );
void UI_SetActiveMenu( const char* menuname,const char *menuID ) 
{
#ifdef _XBOX
	XBLF("STEFX_INPUT_UI_SetActiveMenu request menu='%s' menuID='%s' clsState=%d catcher=0x%x paused='%s'",
		menuname ? menuname : "<null>",
		menuID ? menuID : "",
		cls.state,
		ui.Key_GetCatcher(),
		UI_Cvar_VariableString("cl_paused"));
#endif
	// Sooper-hack. After we play the ja08 cutscene, the game renders for a couple frames.
	// So the cinematic code turns off Present(), and we have to turn it back on here:
	extern bool connectSwapOverride;
	connectSwapOverride = false;

	// this should be the ONLY way the menu system is brought up (besides the UI_ConsoleCommand below)

	if (cls.state != CA_DISCONNECTED && !ui.SG_GameAllowedToSaveHere(qtrue))	//don't check full sytem, only if incamera
	{
#ifdef _XBOX
		XBLF("STEFX: UI_SetActiveMenu blocked reason=GameAllowedToSaveHere menu='%s' menuID='%s' clsState=%d catcher=0x%x",
			menuname ? menuname : "<null>",
			menuID ? menuID : "",
			cls.state,
			ui.Key_GetCatcher());
#endif
		return;
	}

	if ( !menuname ) {
#ifdef _XBOX
		XBLog_Write("STEFX: UI_SetActiveMenu route=forceOff null menu");
#endif
		UI_EFMainMenu_Deactivate();
		UI_ForceMenuOff();
		return;
	}

	//make sure force-speed and slowmodeath doesn't slow down menus - NOTE: they should reset the timescale when the game un-pauses
	Cvar_SetValue( "timescale", 1.0f );

	UI_Cursor_Show(qtrue);

	// enusure minumum menu data is cached
	Menu_Cache();

	if ( Q_stricmp (menuname, "main") == 0
		|| Q_stricmp (menuname, "mainMenu") == 0
		|| Q_stricmp (menuname, "splashMenu") == 0
		)
	{
#ifdef _XBOX
		XBLF("STEFX_INPUT_UI_SetActiveMenu route=EF-main menu='%s'", menuname);
#endif
		UI_EFMainMenu_Open();
		return;
	}

	if ( UI_EFQmenu_RouteMenuName( menuname ) )
	{
#ifdef _XBOX
		XBLF("STEFX_INPUT_UI_SetActiveMenu menu='%s' consumed by EF route", menuname);
#endif
		return;
	}

#ifdef _XBOX
	XBLF("STEFX_INPUT_UI_SetActiveMenu after Menu_Cache menu='%s' menuID='%s' catcher=0x%x",
		menuname,
		menuID ? menuID : "",
		ui.Key_GetCatcher());
#endif

	if ( Q_stricmp (menuname, "ingame") == 0 ) 
	{
		if ( menuID && menuID[0] && ( UI_EFQmenu_ConsoleCommand( menuID ) || UI_EFQmenu_RouteMenuName( menuID ) ) )
		{
#ifdef _XBOX
			XBLF("STEFX_INPUT_UI_SetActiveMenu ingame menuID='%s' consumed by EF route", menuID);
#endif
			return;
		}

		UI_EFMainMenu_Deactivate();
#ifdef _XBOX
		XBLF("STEFX_INPUT_UI_SetActiveMenu route=ingame begin menuID='%s' catcher=0x%x",
			menuID ? menuID : "",
			ui.Key_GetCatcher());
#endif
		ui.Cvar_Set( "cl_paused", "1" );
	//	S_StopAllSounds();

		//NOT USED
		//JLF usually called with menuID == NULL but if 'noController' is the menuID
		// this forces the pause menu open first and then opens the 'noController' menu
		//basically forces the 'ingameMainMenu' open first
	//	if (menuID)
	//		UI_InGameMenu(NULL);
		//END NOT USED
#ifdef _XBOX
		XBLog_Write("STEFX_INPUT_UI_SetActiveMenu route=ingame calling UI_InGameMenu");
#endif
		UI_InGameMenu(menuID);
#ifdef _XBOX
		XBLF("STEFX_INPUT_UI_SetActiveMenu route=ingame done catcher=0x%x paused='%s'",
			ui.Key_GetCatcher(),
			UI_Cvar_VariableString("cl_paused"));
#endif
		return;
	}

	if ( Q_stricmp (menuname, "datapad") == 0 ) 
	{
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
		XBLog_Write("STEFX_INPUT_UI_SetActiveMenu route=datapad suppressed inherited JA datapad shell");
		ui.Cvar_Set( "stefx_objectivesOverlay", "1" );
		return;
#else
		UI_EFMainMenu_Deactivate();
#ifdef _XBOX
		XBLF("STEFX_INPUT_UI_SetActiveMenu route=datapad begin catcher=0x%x", ui.Key_GetCatcher());
#endif
		ui.Cvar_Set( "cl_paused", "1" );
		S_StopAllSoundsExceptMusic();
#ifdef _XBOX
		XBLog_Write("STEFX_INPUT_UI_SetActiveMenu route=datapad calling UI_DataPadMenu");
#endif
		UI_DataPadMenu();
#ifdef _XBOX
		XBLF("STEFX_INPUT_UI_SetActiveMenu route=datapad done catcher=0x%x paused='%s'",
			ui.Key_GetCatcher(),
			UI_Cvar_VariableString("cl_paused"));
#endif
		return;
#endif
	}

	if ( Q_stricmp (menuname, "missionfailed_menu") == 0 ) 
	{
#ifdef _XBOX
		XBLog_Write("STEFX: UI_SetActiveMenu route=missionfailed_menu -> EF load game");
#endif
		ui.Cvar_Set( "cl_paused", "1" );
		ui.Cvar_Set( "ui_missionfailed", "1" );
		UI_EFQmenu_ConsoleCommand("ui_ef_loadgame");
		ui.Key_SetCatcher( KEYCATCH_UI );
		return;
	}
//allows the 'noController' menu and similar menus to 'popup' over existing menu
	if ( Q_stricmp (menuname, "ui_popup") == 0 ) 
	{
#ifdef _XBOX
		XBLF("STEFX: UI_SetActiveMenu route=ui_popup menuID='%s'", menuID ? menuID : "");
#endif
		if ( UI_EFQmenu_RouteMenuName( menuID ) )
		{
#ifdef _XBOX
			XBLF("STEFX: UI_SetActiveMenu ui_popup menuID='%s' consumed by EF route", menuID ? menuID : "");
#endif
			return;
		}
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
		XBLF("STEFX: UI_SetActiveMenu blocked inherited ui_popup menuID='%s'", menuID ? menuID : "");
		return;
#else
		Menus_ActivateByName(menuID);	
		return;
#endif
	}
	
	// Elite Force-owned UI routes must be added explicitly above.  Do not fall
	// through to inherited JA parser menus for unresolved frontend/load names.
#ifdef _XBOX
	XBLF("STEFX: UI_SetActiveMenu missing EF menu route='%s' menuID='%s' catcher=0x%x",
		menuname ? menuname : "",
		menuID ? menuID : "",
		ui.Key_GetCatcher());
#endif
	return;

}


/*
=================
UI_Argv
=================
*/
static char *UI_Argv( int arg ) 
{
	static char	buffer[MAX_STRING_CHARS];

	ui.Argv( arg, buffer, sizeof( buffer ) );

	return buffer;
}


/*
=================
UI_Cvar_VariableString
=================
*/
char *UI_Cvar_VariableString( const char *var_name ) 
{
	static char	buffer[MAX_STRING_CHARS];

	ui.Cvar_VariableStringBuffer( var_name, buffer, sizeof( buffer ) );

	return buffer;
}

/*
=================
UI_Cache
=================
*/
static void UI_Cache_f( void ) 
{
#ifdef _XBOX
	XBLog_Write("STEFX: UI_Cache_f using EF frontend/pause caches; replaced inherited JA mission-select preload");
#endif
	Menu_Cache();
	UI_EFMainMenu_Cache();
	UI_EFPauseMenu_Cache();
}


/*
=================
UI_ConsoleCommand
=================
*/
void UI_Load(void);	//in UI_main.cpp

qboolean UI_ConsoleCommand( void ) 
{
	char	*cmd;

	if (!ui.SG_GameAllowedToSaveHere(qtrue))	//only check if incamera
	{
		return qfalse;
	}

	cmd = UI_Argv( 0 );

	// ensure minimum menu data is available
	Menu_Cache();

	if ( Q_stricmp (cmd, "ui_cache") == 0 ) 
	{
		UI_Cache_f();
		return qtrue;
	}

	if ( Q_stricmp (cmd, "levelselect") == 0 ) 
	{
		UI_LoadMenu_f();
		return qtrue;
	}
	
	if ( Q_stricmp (cmd, "ui_teamOrders") == 0 ) 
	{
		UI_SaveMenu_f();
		return qtrue;
	}

	if ( Q_stricmp (cmd, "ui_report") == 0 ) 
	{
		UI_Report();
		return qtrue;
	}
	
	if ( Q_stricmp (cmd, "ui_load") == 0 ) 
	{
		UI_Load();
		return qtrue;
	}

	if ( UI_EFQmenu_ConsoleCommand( cmd ) )
	{
#ifdef _XBOX
		XBLF("STEFX: EF frontend command '%s' handled by EF qmenu bridge", cmd);
#endif
		return qtrue;
	}

	return qfalse;
}


/*
=================
UI_Init
=================
*/
void UI_Init( int apiVersion, uiimport_t *uiimport, qboolean inGameLoad ) 
{
#ifdef _XBOX
	XBLF("JA: UI_Init entered api=%d expected=%d inGameLoad=%d uiimport=%p",
		apiVersion,
		UI_API_VERSION,
		(int)inGameLoad,
		(void*)uiimport);
#endif
	ui = *uiimport;

	if ( apiVersion != UI_API_VERSION ) {
		ui.Error( ERR_FATAL, "Bad UI_API_VERSION: expected %i, got %i\n", UI_API_VERSION, apiVersion );
	}

	// get static data (glconfig, media)
#ifdef _XBOX
	XBLog_Write("JA: UI_Init: GetGlconfig...");
#endif
	ui.GetGlconfig( &uis.glconfig );
#ifdef _XBOX
	XBLF("JA: UI_Init: glconfig %dx%d", uis.glconfig.vidWidth, uis.glconfig.vidHeight);
#endif

	uis.scaley = uis.glconfig.vidHeight * (1.0/480.0);
	uis.scalex = uis.glconfig.vidWidth * (1.0/640.0);

#ifdef _XBOX
	XBLog_Write("JA: UI_Init: Menu_Cache...");
#endif
	Menu_Cache( );
#ifdef _XBOX
	XBLog_Write("JA: UI_Init: Menu_Cache done; creating cvars...");
#endif

	ui.Cvar_Create( "cg_drawCrosshair", "1", CVAR_ARCHIVE );
	ui.Cvar_Create( "cg_marks", "1", CVAR_ARCHIVE );
//	ui.Cvar_Create ("s_language",			"english",	CVAR_ARCHIVE | CVAR_NORESTART);
	ui.Cvar_Create( "g_char_model",			"jedi_tf",	CVAR_ARCHIVE|CVAR_SAVEGAME|CVAR_NORESTART );
	ui.Cvar_Create( "g_char_skin_head",		"head_a1",	CVAR_ARCHIVE|CVAR_SAVEGAME|CVAR_NORESTART );
	ui.Cvar_Create( "g_char_skin_torso",	"torso_a1",	CVAR_ARCHIVE|CVAR_SAVEGAME|CVAR_NORESTART );
	ui.Cvar_Create( "g_char_skin_legs",		"lower_a1",	CVAR_ARCHIVE|CVAR_SAVEGAME|CVAR_NORESTART );
	ui.Cvar_Create( "g_char_color_red",		"255",		CVAR_ARCHIVE|CVAR_SAVEGAME|CVAR_NORESTART );
	ui.Cvar_Create( "g_char_color_green",	"255",		CVAR_ARCHIVE|CVAR_SAVEGAME|CVAR_NORESTART );
	ui.Cvar_Create( "g_char_color_blue",	"255",		CVAR_ARCHIVE|CVAR_SAVEGAME|CVAR_NORESTART );
	ui.Cvar_Create( "g_saber_type",			"single",	CVAR_ARCHIVE|CVAR_SAVEGAME|CVAR_NORESTART );
	ui.Cvar_Create( "g_saber",				"single_1",	CVAR_ARCHIVE|CVAR_SAVEGAME|CVAR_NORESTART );
	ui.Cvar_Create( "g_saber2",				"",			CVAR_ARCHIVE|CVAR_SAVEGAME|CVAR_NORESTART );
	ui.Cvar_Create( "g_saber_color",		"yellow",	CVAR_ARCHIVE|CVAR_SAVEGAME|CVAR_NORESTART );
	ui.Cvar_Create( "g_saber2_color",		"yellow",	CVAR_ARCHIVE|CVAR_SAVEGAME|CVAR_NORESTART );
	
	ui.Cvar_Create( "ui_forcepower_inc",	"0",		CVAR_ROM|CVAR_SAVEGAME|CVAR_NORESTART);
	ui.Cvar_Create( "tier_storyinfo",		"0",		CVAR_ROM|CVAR_SAVEGAME|CVAR_NORESTART);
	ui.Cvar_Create( "tiers_complete",		"",			CVAR_ROM|CVAR_SAVEGAME|CVAR_NORESTART);
	ui.Cvar_Create( "ui_prisonerobj_currtotal", "0",	CVAR_ROM|CVAR_SAVEGAME|CVAR_NORESTART);
	ui.Cvar_Create( "ui_prisonerobj_mintotal",  "0",	CVAR_ROM|CVAR_SAVEGAME|CVAR_NORESTART);

	ui.Cvar_Create( "g_dismemberment", "3", CVAR_ARCHIVE );//0 = none, 1 = arms and hands, 2 = legs, 3 = waist and head, 4 = mega dismemberment
	ui.Cvar_Create( "cg_gunAutoFirst", "1", CVAR_ARCHIVE );
	ui.Cvar_Create( "cg_crosshairIdentifyTarget", "1", CVAR_ARCHIVE );
	ui.Cvar_Create( "g_subtitles", "0", CVAR_ARCHIVE );
	ui.Cvar_Create( "cg_marks", "1", CVAR_ARCHIVE );
	ui.Cvar_Create( "d_slowmodeath", "3", CVAR_ARCHIVE );
	ui.Cvar_Create( "cg_shadows", "1", CVAR_ARCHIVE );

	ui.Cvar_Create( "cg_runpitch", "0.002", CVAR_ARCHIVE );
	ui.Cvar_Create( "cg_runroll", "0.005", CVAR_ARCHIVE );
	ui.Cvar_Create( "cg_bobup", "0.005", CVAR_ARCHIVE );
	ui.Cvar_Create( "cg_bobpitch", "0.002", CVAR_ARCHIVE );
	ui.Cvar_Create( "cg_bobroll", "0.002", CVAR_ARCHIVE );

	ui.Cvar_Create( "ui_disableWeaponSway", "0", CVAR_ARCHIVE );

	

#ifdef _XBOX
	XBLog_Write("JA: UI_Init: calling _UI_Init...");
#endif
	_UI_Init(inGameLoad);
#ifdef _XBOX
	XBLog_Write("JA: UI_Init done");
#endif
}

// these are only here so the functions in q_shared.c can link

#ifndef UI_HARD_LINKED

/*
================
Com_Error
=================
*/
/*
void Com_Error( int level, const char *error, ... ) 
{
	va_list		argptr;
	char		text[1024];

	va_start (argptr, error);
	vsprintf (text, error, argptr);
	va_end (argptr);

	ui.Error( level, "%s", text);
}
*/
/*
================
Com_Printf
=================
*/
/*
void Com_Printf( const char *msg, ... ) 
{
	va_list		argptr;
	char		text[1024];

	va_start (argptr, msg);
	vsprintf (text, msg, argptr);
	va_end (argptr);

	ui.Printf( "%s", text);
}
*/
#endif


/*
================
UI_DrawNamedPic
=================
*/
void UI_DrawNamedPic( float x, float y, float width, float height, const char *picname ) 
{
	qhandle_t	hShader;

	hShader = ui.R_RegisterShaderNoMip( picname );
	ui.R_DrawStretchPic( x, y, width, height, 0, 0, 1, 1, hShader );
}


/*
================
UI_DrawHandlePic
=================
*/
void UI_DrawHandlePic( float x, float y, float w, float h, qhandle_t hShader ) 
{
	float	s0;
	float	s1;
	float	t0;
	float	t1;

	if( w < 0 ) {	// flip about horizontal
		w  = -w;
		s0 = 1;
		s1 = 0;
	}
	else {
		s0 = 0;
		s1 = 1;
	}

	if( h < 0 ) {	// flip about vertical
		h  = -h;
		t0 = 1;
		t1 = 0;
	}
	else {
		t0 = 0;
		t1 = 1;
	}

	ui.R_DrawStretchPic( x, y, w, h, s0, t0, s1, t1, hShader );
}

/*
================
UI_FillRect

Coordinates are 640*480 virtual values
=================
*/
void UI_FillRect( float x, float y, float width, float height, const float *color ) 
{
	ui.R_SetColor( color );

	ui.R_DrawStretchPic( x, y, width, height, 0, 0, 0, 0, uis.whiteShader );

	ui.R_SetColor( NULL );
}

/*
=================
UI_UpdateScreen
=================
*/
void UI_UpdateScreen( void ) 
{
	ui.UpdateScreen();
}


/*
===============
UI_LoadMenu_f
===============
*/
static void UI_LoadMenu_f( void ) 
{
#ifdef _XBOX
	XBLog_Write("STEFX: UI_LoadMenu_f routing to EF qmenu load screen");
#endif
	UI_EFQmenu_ConsoleCommand("ui_ef_loadgame");
}

/*
===============
UI_SaveMenu_f
===============
*/
static void UI_SaveMenu_f( void )
{
//	ui.PrecacheScreenshot();

#ifdef _XBOX
	XBLog_Write("STEFX: UI_SaveMenu_f routing to EF qmenu save screen");
#endif
	UI_EFQmenu_ConsoleCommand("ui_ef_savegame");
}


//--------------------------------------------

/*
=================
UI_SetColor
=================
*/
void UI_SetColor( const float *rgba ) 
{
	trap_R_SetColor( rgba );
}

/*int registeredFontCount = 0;
#define MAX_FONTS 6
static fontInfo_t registeredFont[MAX_FONTS];
*/

/*
=================
UI_RegisterFont
=================
*/

int UI_RegisterFont(const char *fontName) 
{
	int iFontIndex = ui.R_RegisterFont(fontName);
	if (iFontIndex == 0)
	{
		iFontIndex = ui.R_RegisterFont("ergoec");	// fall back
	}

	return iFontIndex;
}

