// cg_consolecmds.c -- text commands typed in at the local console, or
// executed by a key binding

// this line must stay at top so the whole PCH thing works...
#include "cg_headers.h"

//#include "cg_local.h"
#include "cg_media.h"	//just for cgs....

void CG_TargetCommand_f( void );
extern qboolean	player_locked;
extern void CMD_CGCam_Disable( void );
void CG_NextInventory_f( void );
void CG_PrevInventory_f( void );
void CG_NextForcePower_f( void );
void CG_PrevForcePower_f( void );
void CG_LoadHud_f( void );

#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
extern int statusTextIndex;
extern void CG_MissionFailed(void);

static void CG_STEFX_TestPause_f( void )
{
	Com_PrintfAlways("STEFX_MODAL_TEST pause trigger time=%d paused=%d missionStatus=%d\n",
		cg.time,
		cg_paused.integer,
		cg.missionStatusShow ? 1 : 0);
	cgi_UI_SetActive_Menu("ingame");
}

static void CG_STEFX_TestObjectives_f( void )
{
	Com_PrintfAlways("STEFX_MODAL_TEST objectives trigger time=%d paused=%d missionStatus=%d\n",
		cg.time,
		cg_paused.integer,
		cg.missionStatusShow ? 1 : 0);
	cgi_Cvar_Set("stefx_objectivesOverlay", "1");
	cgi_SendConsoleCommand("+info\n");
}

static void CG_STEFX_TestMissionFailed_f( void )
{
	Com_PrintfAlways("STEFX_MODAL_TEST missionfailed trigger time=%d paused=%d missionStatus=%d failedScreen=%d\n",
		cg.time,
		cg_paused.integer,
		cg.missionStatusShow ? 1 : 0,
		cg.missionFailedScreen ? 1 : 0);

	statusTextIndex = -1;
	cg.missionStatusShow = qtrue;
	cg.missionStatusDeadTime = cg.time + 1000;
	cg.missionFailedScreen = qfalse;
	CG_MissionFailed();
}
#endif

/*
====================
CG_ColorFromString
====================
*/
/*
static void CG_SetColor_f( void) {

	if (cgi_Argc()==4)
	{
		g_entities[0].client->renderInfo.customRGBA[0] = atoi( CG_Argv(1) );
		g_entities[0].client->renderInfo.customRGBA[1] = atoi( CG_Argv(2) );
		g_entities[0].client->renderInfo.customRGBA[2] = atoi( CG_Argv(3) );
	}
	if (cgi_Argc()==2)
	{
		int val = atoi( CG_Argv(1) );
				
		if ( val < 1 || val > 7 ) {
			g_entities[0].client->renderInfo.customRGBA[0] = 255;
			g_entities[0].client->renderInfo.customRGBA[1] = 255;
			g_entities[0].client->renderInfo.customRGBA[2] = 255;
			return;
		}
		g_entities[0].client->renderInfo.customRGBA[0]=0;
		g_entities[0].client->renderInfo.customRGBA[1]=0;
		g_entities[0].client->renderInfo.customRGBA[2]=0;
		
		if ( val & 1 ) {
			g_entities[0].client->renderInfo.customRGBA[2] = 255;
		}
		if ( val & 2 ) {
			g_entities[0].client->renderInfo.customRGBA[1] = 255;
		}
		if ( val & 4 ) {
			g_entities[0].client->renderInfo.customRGBA[0] = 255;
		}
	}
}
*/
/*
=============
CG_Viewpos_f

Debugging command to print the current position
=============
*/
static void CG_Viewpos_f (void) {
	CG_Printf ("%s (%i %i %i) : %i\n", cgs.mapname, (int)cg.refdef.vieworg[0],
		(int)cg.refdef.vieworg[1], (int)cg.refdef.vieworg[2], 
		(int)cg.refdefViewAngles[YAW]);
}

void CG_WriteCam_f (void)
{
	char	text[1024];
	char	*targetname;
	static	int	numCams;

	numCams++;
	
	targetname = (char	*)CG_Argv(1);

	if( !targetname || !targetname[0] )
	{
		targetname = "nameme!";
	}

	CG_Printf( "Camera #%d ('%s') written to: ", numCams, targetname );
	sprintf( text, "//entity %d\n{\n\"classname\"	\"ref_tag\"\n\"targetname\"	\"%s\"\n\"origin\" \"%i %i %i\"\n\"angles\" \"%i %i %i\"\n\"fov\" \"%i\"\n}\n", numCams, targetname, (int)cg.refdef.vieworg[0], (int)cg.refdef.vieworg[1], (int)cg.refdef.vieworg[2], (int)cg.refdefViewAngles[0], (int)cg.refdefViewAngles[1], (int)cg.refdefViewAngles[2], cg_fov.integer );
	gi.WriteCam( text );
}

void Lock_Disable ( void )
{
	player_locked = qfalse;
}

extern float cg_zoomFov;	//from cg_view.cpp

void CG_ToggleBinoculars( void )
{
	if ( in_camera || !cg.snap )
	{
		return;
	}

	if ( cg.zoomMode == 0 || cg.zoomMode >= 2 ) // not zoomed or currently zoomed with the disruptor or LA goggles
	{
		if ( (cg.snap->ps.saber[0].Active() && cg.snap->ps.saberInFlight) || cg.snap->ps.stats[STAT_HEALTH] <= 0)
		{//can't select binoculars when throwing saber
			//FIXME: indicate this to the player
			return;
		}

		if ( cg.snap->ps.viewEntity || ( cg_entities[cg.snap->ps.clientNum].currentState.eFlags & ( EF_LOCKED_TO_WEAPON | EF_IN_ATST )))
		{
			// can't zoom when you have a viewEntity or driving an atst or in an emplaced gun
			return;
		}

		cg.zoomMode = 1;
		cg.zoomLocked = qfalse;

		if ( cg.snap->ps.batteryCharge )
		{
			// when you have batteries, you can actually zoom in
			cg_zoomFov = 40.0f;
		}
		else if ( cg.overrides.active & CG_OVERRIDE_FOV )
		{
			cg_zoomFov = cg.overrides.fov;
		}
		else
		{
			cg_zoomFov = cg_fov.value;
		}

		cgi_S_StartSound( NULL, cg.snap->ps.clientNum, CHAN_AUTO, cgs.media.zoomStart );
#ifdef _IMMERSION
		cgi_FF_Start( cgs.media.zoomStartForce, cg.snap->ps.clientNum );
#endif // _IMMERSION
	}
	else
	{
		cg.zoomMode = 0;
		cg.zoomTime = cg.time;
		cgi_S_StartSound( NULL, cg.snap->ps.clientNum, CHAN_AUTO, cgs.media.zoomEnd );
#ifdef _IMMERSION
		cgi_FF_Start( cgs.media.zoomEndForce, cg.snap->ps.clientNum );
#endif // _IMMERSION
	}
}

void CG_ToggleLAGoggles( void )
{
	if ( in_camera || !cg.snap)
	{
		return;
	}

	if ( cg.zoomMode == 0 || cg.zoomMode < 3 ) // not zoomed or currently zoomed with the disruptor or regular binoculars
	{
		if ( (cg.snap->ps.saber[0].Active() && cg.snap->ps.saberInFlight) || cg.snap->ps.stats[STAT_HEALTH] <= 0 )
		{//can't select binoculars when throwing saber
			//FIXME: indicate this to the player
			return;
		}

		if ( cg.snap->ps.viewEntity || ( cg_entities[cg.snap->ps.clientNum].currentState.eFlags & ( EF_LOCKED_TO_WEAPON | EF_IN_ATST )))
		{
			// can't zoom when you have a viewEntity or driving an atst or in an emplaced gun
			return;
		}

		cg.zoomMode = 3;
		cg.zoomLocked = qfalse;
		if ( cg.overrides.active & CG_OVERRIDE_FOV )
		{
			cg_zoomFov = cg.overrides.fov;
		}
		else
		{
			cg_zoomFov = cg_fov.value; // does not zoom!!
		}

		cgi_S_StartSound( NULL, cg.snap->ps.clientNum, CHAN_AUTO, cgs.media.zoomStart );
#ifdef _IMMERSION
		cgi_FF_Start( cgs.media.zoomStartForce, cg.snap->ps.clientNum );
#endif // _IMMERSION
	}
	else
	{
		cg.zoomMode = 0;
		cg.zoomTime = cg.time;
		cgi_S_StartSound( NULL, cg.snap->ps.clientNum, CHAN_AUTO, cgs.media.zoomEnd );
#ifdef _IMMERSION
		cgi_FF_Start( cgs.media.zoomEndForce, cg.snap->ps.clientNum );
#endif // _IMMERSION
	}
}

void CG_ZoomDown_f( void )
{
	CG_ToggleBinoculars();
}

void CG_ZoomUp_f( void )
{
}

static void CG_ZoomOff_f( void )
{
	if ( cg.zoomMode != 0 )
	{
		cg.zoomMode = 0;
		cg.zoomTime = cg.time;
		cgi_S_StartSound( NULL, cg.snap ? cg.snap->ps.clientNum : ENTITYNUM_WORLD, CHAN_AUTO, cgs.media.zoomEnd );
#ifdef _IMMERSION
		if ( cg.snap )
		{
			cgi_FF_Start( cgs.media.zoomEndForce, cg.snap->ps.clientNum );
		}
#endif // _IMMERSION
	}
}

static void CG_InfoDown_f( void ) {
	cg.showInformation = qtrue;
#ifdef _XBOX
	cgi_Cvar_Set("stefx_objectivesOverlay", "1");
	Com_PrintfAlways("STEFX: CG_InfoDown_f showInformation=1 time=%d\n", cg.time);
#endif
}

static void CG_InfoUp_f( void ) 
{
	cg.showInformation = qfalse;
#ifdef _XBOX
	cgi_Cvar_Set("stefx_objectivesOverlay", "0");
	Com_PrintfAlways("STEFX: CG_InfoUp_f showInformation=0 time=%d\n", cg.time);
#endif
}

typedef struct {
	char	*cmd;
	void	(*function)(void);
} consoleCommand_t;


static consoleCommand_t	commands[] = {
	{ "testmodel", CG_TestModel_f },
	{ "nextframe", CG_TestModelNextFrame_f },
	{ "prevframe", CG_TestModelPrevFrame_f },
	{ "nextskin", CG_TestModelNextSkin_f },
	{ "prevskin", CG_TestModelPrevSkin_f },
/*
Ghoul2 Insert Start
*/
	{ "testG2Model", CG_TestG2Model_f},
	{ "testsurface", CG_TestModelSurfaceOnOff_f },
	{ "testanglespre", CG_TestModelSetAnglespre_f},
	{ "testanglespost", CG_TestModelSetAnglespost_f},
	{ "testanimate", CG_TestModelAnimate_f},
	{ "testlistbones", CG_ListModelBones_f},
	{ "testlistsurfaces", CG_ListModelSurfaces_f},
/*
Ghoul2 Insert End
*/
	{ "viewpos", CG_Viewpos_f },
	{ "writecam", CG_WriteCam_f },
	{ "+info", CG_InfoDown_f },
	{ "-info", CG_InfoUp_f },
	{ "weapnext", CG_NextWeapon_f },
	{ "weapprev", CG_PrevWeapon_f },
	{ "weapon", CG_Weapon_f },
	{ "tcmd", CG_TargetCommand_f },
	{ "cam_disable", CMD_CGCam_Disable },	//gets out of camera mode for debuggin
	{ "cam_enable", CGCam_Enable },	//gets into camera mode for precise camera placement
	{ "lock_disable", Lock_Disable },	//player can move now
	{ "+zoom", CG_ZoomDown_f },
	{ "-zoom", CG_ZoomUp_f },
	{ "zoom", CG_ToggleBinoculars },
	{ "zoomoff", CG_ZoomOff_f },
	{ "la_zoom", CG_ToggleLAGoggles },
	{ "invnext", CG_NextInventory_f },
	{ "invprev", CG_PrevInventory_f },
	{ "forcenext", CG_NextForcePower_f },
	{ "forceprev", CG_PrevForcePower_f },
	{ "loadhud", CG_LoadHud_f },
	{ "dpweapnext", CG_DPNextWeapon_f },
	{ "dpweapprev", CG_DPPrevWeapon_f },
	{ "dpinvnext", CG_DPNextInventory_f },
	{ "dpinvprev", CG_DPPrevInventory_f },
	{ "dpforcenext", CG_DPNextForcePower_f },
	{ "dpforceprev", CG_DPPrevForcePower_f },
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
	{ "stefx_test_pause", CG_STEFX_TestPause_f },
	{ "stefx_test_objectives", CG_STEFX_TestObjectives_f },
	{ "stefx_test_missionfailed", CG_STEFX_TestMissionFailed_f },
#endif
//	{ "color", CG_SetColor_f },
};


//extern menuDef_t *menuScoreboard;
void Menu_Reset();	

void CG_LoadHud_f( void) 
{
	const char *hudSet;

//	cgi_UI_String_Init();

//	cgi_UI_Menu_Reset();
	
	hudSet = cg_hudFiles.string;
	if (hudSet[0] == '\0') 
	{
		hudSet = "ui/jahud.txt";
	}

	CG_LoadMenus(hudSet);
//	menuScoreboard = NULL;

}

/*
=================
CG_ConsoleCommand

The string has been tokenized and can be retrieved with
Cmd_Argc() / Cmd_Argv()
=================
*/
qboolean CG_ConsoleCommand( void ) {
	const char	*cmd;
	int		i;

	cmd = CG_Argv(0);

	for ( i = 0 ; i < sizeof( commands ) / sizeof( commands[0] ) ; i++ ) {
		if ( !Q_stricmp( cmd, commands[i].cmd ) ) {
			commands[i].function();
			return qtrue;
		}
	}

	return qfalse;
}


/*
=================
CG_InitConsoleCommands

Let the client system know about all of our commands
so it can perform tab completion
=================
*/
void CG_InitConsoleCommands( void ) {
	int		i;

	for ( i = 0 ; i < sizeof( commands ) / sizeof( commands[0] ) ; i++ ) {
		cgi_AddCommand( commands[i].cmd );
	}
}
