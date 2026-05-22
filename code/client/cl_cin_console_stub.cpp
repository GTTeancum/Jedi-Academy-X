/*****************************************************************************
 * Temporary Xbox cinematic stub for vc71/XDK build migration.
 *****************************************************************************/

#include "client.h"
#include "../win32/xb_log.h"
#include <stdio.h>

static int currentHandle = -1;
static qboolean qbInGameCinematicOnStandBy = qfalse;
static char sInGameCinematicStandingBy[MAX_QPATH];

static void CinStubLog(const char* msg)
{
	XBLog_Write(msg);
}

static void CinStubLogName(const char* prefix, const char* name)
{
	char msg[256];
	_snprintf(msg, sizeof(msg) - 1, "%s%s", prefix, name ? name : "<null>");
	msg[sizeof(msg) - 1] = '\0';
	XBLog_Write(msg);
}

void CIN_CloseAllVideos(void)
{
	if (currentHandle != -1)
	{
		CinStubLog("CIN stub: CloseAllVideos");
	}
	currentHandle = -1;
}

e_status CIN_StopCinematic(int handle)
{
	if (handle == currentHandle && handle != -1)
	{
		CinStubLog("CIN stub: StopCinematic");
	}
	currentHandle = -1;
	return FMV_EOF;
}

e_status CIN_RunCinematic (int handle)
{
	if (handle != currentHandle || handle == -1)
	{
		return FMV_EOF;
	}
	return FMV_EOF;
}

int CIN_PlayCinematic(const char *arg0, int xpos, int ypos, int width, int height, int bits, const char *psAudioFile /* = NULL */)
{
	currentHandle = 0;
	CinStubLogName("CIN stub: PlayCinematic ", arg0);
	return currentHandle;
}

void CIN_SetExtents (int handle, int x, int y, int w, int h)
{
}

void SCR_DrawCinematic (void)
{
	if (CL_InGameCinematicOnStandBy())
	{
		CinStubLogName("CIN stub: standby cinematic ", sInGameCinematicStandingBy);
		qbInGameCinematicOnStandBy = qfalse;
		sInGameCinematicStandingBy[0] = '\0';
	}
}

void SCR_RunCinematic (void)
{
}

void SCR_StopCinematic(qboolean bAllowRefusal /* = qfalse */)
{
	CIN_StopCinematic(currentHandle);
}

void CIN_UploadCinematic(int handle)
{
}

bool CIN_PlayAllFrames(const char *arg, int x, int y, int w, int h, int systemBits, bool keyBreakAllowed)
{
	CinStubLogName("CIN stub: PlayAllFrames ", arg);
	return false;
}

void CIN_Init(void)
{
	CinStubLog("CIN stub: Init");
}

void CIN_Shutdown(void)
{
	CinStubLog("CIN stub: Shutdown");
}

void CL_PlayCinematic_f(void)
{
	char *arg = Cmd_Argv(1);
	CIN_PlayAllFrames(arg, 48, 36, 544, 408, 0, true);
}

qboolean CL_IsRunningInGameCinematic(void)
{
	return qfalse;
}

void CL_PlayInGameCinematic_f(void)
{
	char *arg = Cmd_Argv(1);

	if (cls.state == CA_ACTIVE)
	{
		CIN_PlayAllFrames(arg, 48, 36, 544, 408, 0, true);
	}
	else
	{
		qbInGameCinematicOnStandBy = qtrue;
		if (arg)
		{
			Q_strncpyz(sInGameCinematicStandingBy, arg, sizeof(sInGameCinematicStandingBy));
		}
		else
		{
			sInGameCinematicStandingBy[0] = '\0';
		}
	}
}

qboolean CL_InGameCinematicOnStandBy(void)
{
	return qbInGameCinematicOnStandBy;
}

void MuteBinkSystem( void )
{
}
