#ifndef STEFX_SPUI_COMPAT_H
#define STEFX_SPUI_COMPAT_H

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "../game/q_shared.h"
#include "../qcommon/qcommon.h"
#include "../qcommon/xb_settings.h"
#include "../win32/xb_log.h"
#include "keycodes.h"
#include "ui_public.h"
#include "ui_ef_qmenu_shared.h"

typedef struct
{
	int qhMediumFont;
} stefxUiAssets_t;

typedef struct
{
	stefxUiAssets_t Assets;
} displayContextDef_t;

typedef struct
{
	displayContextDef_t uiDC;
} uiInfo_t;

struct menuDef_t;
struct itemDef_s;

typedef struct
{
	int frametime;
	int realtime;
	int cursorx;
	int cursory;
	int menusp;
	menuframework_s *activemenu;
	menuframework_s *stack[MAX_MENUDEPTH];
	qboolean debugMode;
	qhandle_t whiteShader;
	qhandle_t menuBackShader;
	qhandle_t cursor;
	float scalex;
	float scaley;
	qboolean firstdraw;
} uiStatic_t;

typedef struct
{
	void (*Printf)(const char *fmt, ...);
	void (*Error)(int level, const char *fmt, ...);
	void (*Cvar_Set)(const char *name, const char *value);
	float (*Cvar_VariableValue)(const char *name);
	void (*Cvar_VariableStringBuffer)(const char *name, char *buffer, int bufsize);
	void (*Cvar_SetValue)(const char *name, float value);
	int (*FS_FOpenFile)(const char *qpath, fileHandle_t *f, fsMode_t mode);
	int (*FS_Read)(void *buffer, int len, fileHandle_t f);
	void (*FS_FCloseFile)(fileHandle_t f);
	int (*FS_ReadFile)(const char *name, void **buf);
	void (*FS_FreeFile)(void *buf);
	qhandle_t (*R_RegisterShader)(const char *name);
	qhandle_t (*R_RegisterShaderNoMip)(const char *name);
	qhandle_t (*R_RegisterFont)(const char *name);
	int (*R_Font_StrLenPixels)(const char *text, const int setIndex, const float scale);
	void (*R_Font_DrawString)(int ox, int oy, const char *text, const float *rgba, const int setIndex, int iMaxPixelWidth, const float scale);
	void (*R_SetColor)(const float *rgba);
	void (*R_DrawStretchPic)(float x, float y, float w, float h, float s1, float t1, float s2, float t2, qhandle_t hShader);
	void (*S_StartLocalSound)(sfxHandle_t sfx, int channelNum);
	void (*Cmd_ExecuteText)(int exec_when, const char *text);
	int (*Key_GetCatcher)(void);
	void (*Key_SetCatcher)(int catcher);
	int (*Milliseconds)(void);
	qboolean (*SG_GameAllowedToSaveHere)(qboolean inCamera);
} uiimport_t;

#include "../namespace_begin.h"

extern uiimport_t stefx_ui_import;
extern uiStatic_t uis;
extern uiInfo_t uiInfo;
extern qboolean inHandler;
extern volatile unsigned int g_SPXBUIPauseOpenCount;
extern volatile unsigned int g_SPXBUIPauseDrawCount;
extern volatile unsigned int g_SPXBUIPauseActive;

#define ui stefx_ui_import

void UI_FillRect(float x, float y, float width, float height, const float *color);
void UI_DrawHandlePic(float x, float y, float w, float h, qhandle_t hShader);
void UI_ForceMenuOff(void);
void UI_EFSP_Init(qboolean inGameLoad);
void UI_EFSP_CacheBase(void);
void UI_EFSP_Refresh(int realtime);
void UI_EFSP_KeyEvent(int key, qboolean down);
void UI_EFSP_SetActiveMenu(uiMenuCommand_t menu);
qboolean UI_EFSP_ConsoleCommand(int realtime);
qboolean UI_EFSP_IsFullscreen(void);
int UI_EFSP_VmMain(int command, int arg0, int arg1, int arg2, int arg3, int arg4, int arg5, int arg6, int arg7, int arg8, int arg9, int arg10, int arg11);
int UI_RegisterFont(const char *fontName);
char *UI_Cvar_VariableString(const char *var_name);
void Text_Paint(float x, float y, float scale, vec4_t color, const char *text, int iMaxPixelWidth, int style, int iFontIndex);
void S_StopAllSoundsExceptMusic(void);
void STEFX_UI_Argv(int n, char *buffer, int bufferLength);

void Menu_Focus(menucommon_s *m);
void Menu_AddItem(menuframework_s *menu, void *item);
void Menu_Center(menuframework_s *menu);
void Menu_Draw(menuframework_s *menu);
void *Menu_ItemAtCursor(menuframework_s *m);
sfxHandle_t Menu_ActivateItem(menuframework_s *s, menucommon_s *item);
void Menu_SetStatusBar(menuframework_s *s, const char *string);
void Menu_SlideItem(menuframework_s *s, int dir);
void Menu_SetCursor(menuframework_s *s, int cursor);
void Menu_AdjustCursor(menuframework_s *menu, int dir);
sfxHandle_t Menu_DefaultKey(menuframework_s *s, int key);
void UI_PushMenu(menuframework_s *menu);
void UI_PopMenu(void);
qboolean UI_EFQmenu_IsActive(void);
void UI_EFQmenu_ClearState(const char *reason);
void UI_EFQmenu_Draw(int realtime);
void UI_EFQmenu_KeyEvent(int key, qboolean down);
qboolean UI_EFQmenu_ConsoleCommand(const char *cmd);
qboolean UI_EFQmenu_RouteMenuName(const char *menuName);

qboolean UI_EFMainMenu_IsActive(void);
void UI_EFMainMenu_Cache(void);
void UI_EFMainMenu_InvalidateCache(void);
void UI_EFMainMenu_Open(void);
void UI_EFMainMenu_OpenNewGame(void);
void UI_EFMainMenu_OpenLoadGame(void);
void UI_EFMainMenu_OpenConfigure(void);
void UI_EFMainMenu_OpenAudio(void);
void UI_EFMainMenu_OpenVideo(void);
void UI_EFMainMenu_OpenController(void);
void UI_EFMainMenu_StartSplitScreenBaseline(void);
void UI_EFMainMenu_StartHolomatchBaseline(void);
void UI_EFMainMenu_OpenStub(const char *title, const char *line);
void UI_EFMainMenu_Deactivate(void);
void UI_EFMainMenu_Draw(int realtime);
void UI_EFMainMenu_KeyEvent(int key, qboolean down);
qboolean UI_EFMainMenu_WantsControllerInput(void);
void UI_EFMainMenu_ControllerKeyEvent(int controller, int key, qboolean down);

qboolean UI_EFPauseMenu_IsActive(void);
void UI_EFPauseMenu_Cache(void);
void UI_EFPauseMenu_InvalidateCache(void);
void UI_EFPauseMenu_Open(const char *menuID);
void UI_EFPauseMenu_Deactivate(void);
void UI_EFPauseMenu_Draw(int realtime);
void UI_EFPauseMenu_KeyEvent(int key, qboolean down);

#endif
