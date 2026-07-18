#include <stdio.h>

#include "../game/q_shared.h"
#include "../qcommon/xb_settings.h"

// Holomatch MP keeps this file as the syscall/export adapter only.
// UI state, drawing helpers, text, and menu behavior live in shared code/ui.

#include "ui_stefx_spcompat.h"

static int (QDECL *s_syscall)(int arg, ...) = (int (QDECL *)(int, ...))-1;

static int STEFX_PASSFLOAT(float x)
{
	float temp = x;
	return *(int *)&temp;
}

static void STEFX_UI_Printf(const char *fmt, ...)
{
	char text[2048];
	va_list ap;

	if (!s_syscall || s_syscall == (int (QDECL *)(int, ...))-1)
	{
		return;
	}

	va_start(ap, fmt);
	_vsnprintf(text, sizeof(text) - 1, fmt, ap);
	va_end(ap);
	text[sizeof(text) - 1] = '\0';

	s_syscall(UI_PRINT, text);
}

static void STEFX_UI_Error(int level, const char *fmt, ...)
{
	char text[2048];
	va_list ap;

	if (!s_syscall || s_syscall == (int (QDECL *)(int, ...))-1)
	{
		return;
	}

	va_start(ap, fmt);
	_vsnprintf(text, sizeof(text) - 1, fmt, ap);
	va_end(ap);
	text[sizeof(text) - 1] = '\0';

	(void)level;
	s_syscall(UI_ERROR, text);
}

static void STEFX_UI_CvarSet(const char *name, const char *value)
{
	s_syscall(UI_CVAR_SET, name, value);
}

static float STEFX_UI_CvarValue(const char *name)
{
	int temp = s_syscall(UI_CVAR_VARIABLEVALUE, name);
	return *(float *)&temp;
}

static void STEFX_UI_CvarStringBuffer(const char *name, char *buffer, int bufsize)
{
	s_syscall(UI_CVAR_VARIABLESTRINGBUFFER, name, buffer, bufsize);
}

static void STEFX_UI_CvarSetValue(const char *name, float value)
{
	s_syscall(UI_CVAR_SETVALUE, name, STEFX_PASSFLOAT(value));
}

static int STEFX_UI_FSOpen(const char *qpath, fileHandle_t *f, fsMode_t mode)
{
	return s_syscall(UI_FS_FOPENFILE, qpath, f, mode);
}

static int STEFX_UI_FSRead(void *buffer, int len, fileHandle_t f)
{
	s_syscall(UI_FS_READ, buffer, len, f);
	return len;
}

static void STEFX_UI_FSClose(fileHandle_t f)
{
	s_syscall(UI_FS_FCLOSEFILE, f);
}

static int STEFX_UI_FSReadFile(const char *name, void **buf)
{
	fileHandle_t f;
	int len;
	void *data;

	if (!buf)
	{
		return -1;
	}

	*buf = NULL;
	len = STEFX_UI_FSOpen(name, &f, FS_READ);
	if (len <= 0)
	{
		return len;
	}

	data = malloc(len + 1);
	if (!data)
	{
		STEFX_UI_FSClose(f);
		return -1;
	}

	STEFX_UI_FSRead(data, len, f);
	((char *)data)[len] = '\0';
	STEFX_UI_FSClose(f);
	*buf = data;
	return len;
}

static void STEFX_UI_FSFreeFile(void *buf)
{
	free(buf);
}

static qhandle_t STEFX_UI_RegisterShaderNoMip(const char *name)
{
	char buffer[1024];

	if (name && name[0] == '*')
	{
		STEFX_UI_CvarStringBuffer(name + 1, buffer, sizeof(buffer));
		if (buffer[0])
		{
			return s_syscall(UI_R_REGISTERSHADERNOMIP, buffer);
		}
	}

	return s_syscall(UI_R_REGISTERSHADERNOMIP, name);
}

static qhandle_t STEFX_UI_RegisterShader(const char *name)
{
	return STEFX_UI_RegisterShaderNoMip(name);
}

static qhandle_t STEFX_UI_RegisterFont(const char *name)
{
	return s_syscall(UI_R_REGISTERFONT, name);
}

static int STEFX_UI_FontStrLenPixels(const char *text, const int setIndex, const float scale)
{
	return s_syscall(UI_R_FONT_STRLENPIXELS, text, setIndex, STEFX_PASSFLOAT(scale));
}

static void STEFX_UI_FontDrawString(int ox, int oy, const char *text, const float *rgba, const int setIndex, int iMaxPixelWidth, const float scale)
{
	s_syscall(UI_R_FONT_DRAWSTRING, ox, oy, text, rgba, setIndex, iMaxPixelWidth, STEFX_PASSFLOAT(scale));
}

static void STEFX_UI_RSetColor(const float *rgba)
{
	s_syscall(UI_R_SETCOLOR, rgba);
}

static void STEFX_UI_DrawStretchPic(float x, float y, float w, float h, float s1, float t1, float s2, float t2, qhandle_t hShader)
{
	s_syscall(UI_R_DRAWSTRETCHPIC,
		STEFX_PASSFLOAT(x),
		STEFX_PASSFLOAT(y),
		STEFX_PASSFLOAT(w),
		STEFX_PASSFLOAT(h),
		STEFX_PASSFLOAT(s1),
		STEFX_PASSFLOAT(t1),
		STEFX_PASSFLOAT(s2),
		STEFX_PASSFLOAT(t2),
		hShader);
}

static void STEFX_UI_StartLocalSound(sfxHandle_t sfx, int channelNum)
{
	s_syscall(UI_S_STARTLOCALSOUND, sfx, channelNum);
}

static void STEFX_UI_CmdExecuteText(int exec_when, const char *text)
{
	s_syscall(UI_CMD_EXECUTETEXT, exec_when, text);
}

static int STEFX_UI_KeyGetCatcher(void)
{
	return s_syscall(UI_KEY_GETCATCHER);
}

static void STEFX_UI_KeySetCatcher(int catcher)
{
	s_syscall(UI_KEY_SETCATCHER, catcher);
}

static int STEFX_UI_Milliseconds(void)
{
	return s_syscall(UI_MILLISECONDS);
}

static qboolean STEFX_UI_CanSave(qboolean inCamera)
{
	(void)inCamera;
	return qfalse;
}

void STEFX_UI_Argv(int n, char *buffer, int bufferLength)
{
	s_syscall(UI_ARGV, n, buffer, bufferLength);
}

static void STEFX_UI_InitImports(void)
{
	memset(&ui, 0, sizeof(ui));
	ui.Printf = STEFX_UI_Printf;
	ui.Error = STEFX_UI_Error;
	ui.Cvar_Set = STEFX_UI_CvarSet;
	ui.Cvar_VariableValue = STEFX_UI_CvarValue;
	ui.Cvar_VariableStringBuffer = STEFX_UI_CvarStringBuffer;
	ui.Cvar_SetValue = STEFX_UI_CvarSetValue;
	ui.FS_FOpenFile = STEFX_UI_FSOpen;
	ui.FS_Read = STEFX_UI_FSRead;
	ui.FS_FCloseFile = STEFX_UI_FSClose;
	ui.FS_ReadFile = STEFX_UI_FSReadFile;
	ui.FS_FreeFile = STEFX_UI_FSFreeFile;
	ui.R_RegisterShader = STEFX_UI_RegisterShader;
	ui.R_RegisterShaderNoMip = STEFX_UI_RegisterShaderNoMip;
	ui.R_RegisterFont = STEFX_UI_RegisterFont;
	ui.R_Font_StrLenPixels = STEFX_UI_FontStrLenPixels;
	ui.R_Font_DrawString = STEFX_UI_FontDrawString;
	ui.R_SetColor = STEFX_UI_RSetColor;
	ui.R_DrawStretchPic = STEFX_UI_DrawStretchPic;
	ui.S_StartLocalSound = STEFX_UI_StartLocalSound;
	ui.Cmd_ExecuteText = STEFX_UI_CmdExecuteText;
	ui.Key_GetCatcher = STEFX_UI_KeyGetCatcher;
	ui.Key_SetCatcher = STEFX_UI_KeySetCatcher;
	ui.Milliseconds = STEFX_UI_Milliseconds;
	ui.SG_GameAllowedToSaveHere = STEFX_UI_CanSave;
}

void dllEntry(int (QDECL *syscallptr)(int arg, ...))
{
	s_syscall = syscallptr;
	STEFX_UI_InitImports();
}

void S_StopAllSoundsExceptMusic(void)
{
	s_syscall(UI_S_STOPBACKGROUNDTRACK);
}

int vmMain(int command, int arg0, int arg1, int arg2, int arg3, int arg4, int arg5, int arg6, int arg7, int arg8, int arg9, int arg10, int arg11)
{
	static qboolean s_loggedBridgeDispatch = qfalse;

	if (!s_loggedBridgeDispatch)
	{
		s_loggedBridgeDispatch = qtrue;
		STEFX_UI_Printf("STEFX_HM: MP UI bridge is syscall adapter only; SP code/ui owns UI framework state and rendering\n");
	}

	return UI_EFSP_VmMain(command, arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10, arg11);
}

#undef ui
#include "../namespace_end.h"

void XB_Startup(XBStartupState startupState)
{
	(void)startupState;
	ui::STEFX_UI_Printf("STEFX_HM: SP EF UI bridge ignored Xbox startup menu request\n");
}

void UI_JoinSession(void)
{
	ui::STEFX_UI_Printf("STEFX_HM: SP EF UI bridge ignored legacy session join UI request\n");
}

void UI_JoinInvite(void)
{
	ui::STEFX_UI_Printf("STEFX_HM: SP EF UI bridge ignored legacy invite UI request\n");
}
