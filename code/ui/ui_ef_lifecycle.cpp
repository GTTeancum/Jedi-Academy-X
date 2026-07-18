// Shared EF/SP UI lifecycle used by Holomatch MP on Xbox.
#if defined(STEFX_ELITE_FORCE_MP)
#include "../../codemp/ui/ui_stefx_spcompat.h"
#else
#include "../server/exe_headers.h"

#include "ui_local.h"
#ifdef _XBOX
#include "../win32/xb_log.h"
#endif
#endif

static qboolean s_loggedInit = qfalse;
static qboolean s_loggedUnsupportedMenuRoute = qfalse;
static qboolean s_loggedSkippedLegacyFont = qfalse;

static void UI_EFSP_ClearOnly(void)
{
	UI_EFMainMenu_Deactivate();
	UI_EFPauseMenu_Deactivate();
	UI_EFQmenu_ClearState("spui-force-off");
	uis.menusp = 0;
	uis.activemenu = NULL;
	memset(uis.stack, 0, sizeof(uis.stack));
}

void UI_ForceMenuOff(void)
{
	int catcher;

	ui.Printf("STEFX_HM: SP UI ForceMenuOff catcherBefore=0x%x\n", ui.Key_GetCatcher());
	UI_EFSP_ClearOnly();
	ui.Cvar_Set("cl_paused", "0");
	catcher = ui.Key_GetCatcher();
	ui.Key_SetCatcher(catcher & ~KEYCATCH_UI);
	ui.Printf("STEFX_HM: SP UI ForceMenuOff catcherAfter=0x%x\n", ui.Key_GetCatcher());
}

void UI_EFSP_CacheBase(void)
{
	if (!uis.whiteShader)
	{
		uis.whiteShader = ui.R_RegisterShader("white");
	}

	if (!uiInfo.uiDC.Assets.qhMediumFont)
	{
#if defined(STEFX_ELITE_FORCE_MP)
		if (!s_loggedSkippedLegacyFont)
		{
			s_loggedSkippedLegacyFont = qtrue;
			ui.Printf("STEFX_HM: SP EF UI cache skipped legacy renderer font; EF prop-font atlas owns Holomatch text\n");
		}
#else
		uiInfo.uiDC.Assets.qhMediumFont = UI_RegisterFont("ergoec");
#endif
	}

	UI_EFMainMenu_Cache();
	UI_EFPauseMenu_Cache();
}

void UI_EFSP_Init(qboolean inGameLoad)
{
	memset(&uis, 0, sizeof(uis));
	memset(&uiInfo, 0, sizeof(uiInfo));
	UI_EFSP_CacheBase();
	(void)inGameLoad;

	if (!s_loggedInit)
	{
		s_loggedInit = qtrue;
		ui.Printf("STEFX_HM: UI mandate active; uniform SP code/ui owns Holomatch UI\n");
		ui.Printf("STEFX_HM: UI mandate enforced; MP legacy menus stay dead and SP code/ui owns all Holomatch UI behavior\n");
		ui.Printf("STEFX_HM: SP EF UI lifecycle initialized from code/ui; no script menu cache; codemp/ui remains adapter-only\n");
	}
}

void UI_EFSP_Refresh(int realtime)
{
	uis.frametime = realtime - uis.realtime;
	uis.realtime = realtime;

	if (UI_EFQmenu_IsActive())
	{
		UI_EFQmenu_Draw(realtime);
		return;
	}

	if (UI_EFPauseMenu_IsActive())
	{
		UI_EFPauseMenu_Draw(realtime);
		return;
	}

	if (UI_EFMainMenu_IsActive())
	{
		UI_EFMainMenu_Draw(realtime);
		return;
	}
}

void UI_EFSP_KeyEvent(int key, qboolean down)
{
	if (UI_EFQmenu_IsActive())
	{
		UI_EFQmenu_KeyEvent(key, down);
		return;
	}

	if (UI_EFPauseMenu_IsActive())
	{
		UI_EFPauseMenu_KeyEvent(key, down);
		return;
	}

	if (UI_EFMainMenu_IsActive())
	{
		UI_EFMainMenu_KeyEvent(key, down);
		return;
	}
}

static void UI_EFSP_RejectUnsupportedMenu(uiMenuCommand_t menu)
{
	if (!s_loggedUnsupportedMenuRoute)
	{
		s_loggedUnsupportedMenuRoute = qtrue;
		ui.Printf("STEFX_HM: SP EF UI rejected unsupported menu command=%d; legacy script menu path is inactive\n", menu);
	}
}

void UI_EFSP_SetActiveMenu(uiMenuCommand_t menu)
{
	UI_EFSP_CacheBase();

	switch (menu)
	{
	case UIMENU_NONE:
	case UIMENU_CLOSEALL:
		UI_ForceMenuOff();
		break;
	case UIMENU_MAIN:
		UI_EFQmenu_ClearState("set-main");
		UI_EFPauseMenu_Deactivate();
		UI_EFMainMenu_Open();
		break;
	case UIMENU_INGAME:
		S_StopAllSoundsExceptMusic();
		UI_EFPauseMenu_Open(NULL);
		break;
	case UIMENU_NOCONTROLLER:
	case UIMENU_NOCONTROLLERINGAME:
		UI_EFMainMenu_OpenStub("ELITE FORCE", "CONTROLLER DISCONNECTED");
		break;
	default:
		UI_EFSP_RejectUnsupportedMenu(menu);
		break;
	}
}

qboolean UI_EFSP_ConsoleCommand(int realtime)
{
	char cmd[MAX_TOKEN_CHARS];
	char arg1[MAX_TOKEN_CHARS];

	uis.frametime = realtime - uis.realtime;
	uis.realtime = realtime;

	STEFX_UI_Argv(0, cmd, sizeof(cmd));
	if (!cmd[0])
	{
		return qfalse;
	}

	if (!Q_stricmpn(cmd, "ui_ef_", 6))
	{
		return UI_EFQmenu_ConsoleCommand(cmd);
	}

	if (!Q_stricmp(cmd, "ui_openmenu") || !Q_stricmp(cmd, "openmenu"))
	{
		STEFX_UI_Argv(1, arg1, sizeof(arg1));
		if (arg1[0] && UI_EFQmenu_RouteMenuName(arg1))
		{
			return qtrue;
		}
		UI_EFSP_RejectUnsupportedMenu((uiMenuCommand_t)-1);
		return qtrue;
	}

	return qfalse;
}

qboolean UI_EFSP_IsFullscreen(void)
{
	return (UI_EFMainMenu_IsActive() || UI_EFPauseMenu_IsActive() || UI_EFQmenu_IsActive()) ? qtrue : qfalse;
}

#if defined(STEFX_ELITE_FORCE_MP)
int UI_EFSP_VmMain(int command, int arg0, int arg1, int arg2, int arg3, int arg4, int arg5, int arg6, int arg7, int arg8, int arg9, int arg10, int arg11)
{
	static qboolean s_loggedSharedVmDispatch = qfalse;

	(void)arg2;
	(void)arg3;
	(void)arg4;
	(void)arg5;
	(void)arg6;
	(void)arg7;
	(void)arg8;
	(void)arg9;
	(void)arg10;
	(void)arg11;

	if (!s_loggedSharedVmDispatch)
	{
		s_loggedSharedVmDispatch = qtrue;
		ui.Printf("STEFX_HM: SP EF UI VM dispatch active from shared code/ui command=%d\n", command);
	}

	switch (command)
	{
	case UI_GETAPIVERSION:
		return UI_API_VERSION;
	case UI_INIT:
		UI_EFSP_Init((qboolean)arg0);
		return 0;
	case UI_SHUTDOWN:
	case UI_MENU_RESET:
		UI_ForceMenuOff();
		return 0;
	case UI_KEY_EVENT:
		UI_EFSP_KeyEvent(arg0, (qboolean)arg1);
		return 0;
	case UI_MOUSE_EVENT:
		return 0;
	case UI_REFRESH:
		UI_EFSP_Refresh(arg0);
		return 0;
	case UI_IS_FULLSCREEN:
		return UI_EFSP_IsFullscreen();
	case UI_SET_ACTIVE_MENU:
		UI_EFSP_SetActiveMenu((uiMenuCommand_t)arg0);
		return 0;
	case UI_CONSOLE_COMMAND:
		return UI_EFSP_ConsoleCommand(arg0);
	case UI_DRAW_CONNECT_SCREEN:
		return 0;
	case UI_HASUNIQUECDKEY:
		return qfalse;
	}

	return -1;
}
#endif

#if defined(STEFX_ELITE_FORCE_MP)
#undef ui
#include "../../codemp/namespace_end.h"
#endif
