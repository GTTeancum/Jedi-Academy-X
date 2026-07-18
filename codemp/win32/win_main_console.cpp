#include "../qcommon/qcommon.h"
#include "../client/client.h"
#include "win_local.h"
#include "resource.h"
#include <float.h>
#include <stdio.h>
#include "../game/g_public.h"
#include "../xbox/XBLive.h"
#include "xb_log.h"

#include "../qcommon/files.h"
#include "win_file.h"
#include "../renderer/tr_local.h"
#include "glw_win_dx8.h"

#include "../qcommon/xb_settings.h"

#ifdef _XBOX
#include "../cgame/cg_local.h"
#include "../client/cl_data.h"

#include <IO.h>
#define NEWDECL __cdecl

#ifndef FINAL_BUILD
#include "dbg_console_xbox.h"
#endif

#endif

#ifndef STEFX_HOLOMATCH_DIRECT_BOOT
#if defined(STEFX_ELITE_FORCE_MP)
#define STEFX_HOLOMATCH_DIRECT_BOOT 1
#else
#define STEFX_HOLOMATCH_DIRECT_BOOT 0
#endif
#endif

#ifndef JAMP_CXBX_SMOKE_STARTUP_COMMAND
#if STEFX_HOLOMATCH_DIRECT_BOOT
#define JAMP_CXBX_SMOKE_STARTUP_COMMAND "+set fs_game BaseEF +set model munro/default +set sv_maxclients 4 +set g_gametype 0 +set fraglimit 0 +set timelimit 0 +set g_ghostRespawn 0 +set g_spawnInvulnerability 0 +set g_forcerespawn 1 +set g_holoIntro 0 +set stefx_hm_directSlice 1 +set bot_enable 1 +set bot_minplayers 3 +set r_uiFullScreen 0 +map hm_borg1"
#else
#define JAMP_CXBX_SMOKE_STARTUP_COMMAND ""
#endif
#endif

#ifndef STEFX_HOLOMATCH_DIRECT_MAP_COMMAND
#if STEFX_HOLOMATCH_DIRECT_BOOT
#define STEFX_HOLOMATCH_DIRECT_MAP_COMMAND "map hm_borg1\n"
#else
#define STEFX_HOLOMATCH_DIRECT_MAP_COMMAND ""
#endif
#endif

#ifndef STEFX_HOLOMATCH_DIRECT_RENDER_CVAR_COMMAND
#if STEFX_HOLOMATCH_DIRECT_BOOT
#define STEFX_HOLOMATCH_DIRECT_RENDER_CVAR_COMMAND "set developer 0\nset r_showtris 0\nset r_shownormals 0\nset r_debugSurface 0\nset r_lightmap 0\nset r_singleShader 0\nset r_nobind 0\n"
#else
#define STEFX_HOLOMATCH_DIRECT_RENDER_CVAR_COMMAND ""
#endif
#endif

#ifndef STEFX_HOLOMATCH_DIRECT_MAP_FRAME
#define STEFX_HOLOMATCH_DIRECT_MAP_FRAME 4
#endif

#ifndef STEFX_HOLOMATCH_DIRECT_BOT_COMMAND
#if STEFX_HOLOMATCH_DIRECT_BOOT
#define STEFX_HOLOMATCH_DIRECT_BOT_COMMAND "addbot 1_of_12 4 free 0\naddbot 2_of_3 4 free 500\n"
#else
#define STEFX_HOLOMATCH_DIRECT_BOT_COMMAND ""
#endif
#endif

#ifndef STEFX_HOLOMATCH_DIRECT_BOT_FRAME
#define STEFX_HOLOMATCH_DIRECT_BOT_FRAME 1
#endif

#ifndef STEFX_HOLOMATCH_DIRECT_BOT_STABLE_FRAMES
#define STEFX_HOLOMATCH_DIRECT_BOT_STABLE_FRAMES 1
#endif

#ifndef STEFX_HOLOMATCH_DIRECT_CLIENT_STABLE_FRAMES
#define STEFX_HOLOMATCH_DIRECT_CLIENT_STABLE_FRAMES 1
#endif

#ifndef JAMP_XEMU_DIRECT_MATCH
#define JAMP_XEMU_DIRECT_MATCH 0
#endif

#ifndef JAMP_ENABLE_MAINLOOP_XBL_TICK
#define JAMP_ENABLE_MAINLOOP_XBL_TICK 0
#endif

#ifndef JAMP_USE_MAINLOOP_SEH
#define JAMP_USE_MAINLOOP_SEH 1
#endif

extern int eventHead, eventTail;
extern sysEvent_t eventQue[MAX_QUED_EVENTS];
extern byte		sys_packetReceived[MAX_MSGLEN];

void *NEWDECL operator new(size_t size)
{
	return Z_Malloc(size, TAG_NEWDEL, qfalse);
}


void *NEWDECL operator new[](size_t size)
{
	return Z_Malloc(size, TAG_NEWDEL, qfalse);
}


void NEWDECL operator delete[](void *ptr)
{
	if (ptr)
		Z_Free(ptr);
}


void NEWDECL operator delete(void *ptr)
{
	if (ptr)
		Z_Free(ptr);
}

/*
================
Sys_Init

Called after the common systems (cvars, files, etc)
are initialized
================
*/
extern void Sys_In_Restart_f(void);
extern void Sys_Net_Restart_f(void);
void Sys_Init( void ) 
{
	Cmd_AddCommand ("in_restart", Sys_In_Restart_f);
	Cmd_AddCommand ("net_restart", Sys_Net_Restart_f);
}




char *Sys_Cwd( void ) {
	static char cwd[MAX_OSPATH];

#ifdef _XBOX
	strcpy(cwd, "d:");
#endif

#ifdef _GAMECUBE
	strcpy(cwd, ".");
#endif

	return cwd;
}

/*
=================
Sys_In_Restart_f

Restart the input subsystem
=================
*/
void Sys_In_Restart_f( void ) {
}



/*
=============
Sys_Error

Show the early console as an error dialog
=============
*/
void Sys_Error( const char *error, ... ) {
        va_list                argptr;
        char                text[256];

        va_start (argptr, error);
        vsprintf (text, error, argptr);
        va_end (argptr);

#ifdef _GAMECUBE
        printf(text);
#else
        OutputDebugString(text);
#endif

#if 0 // UN-PORT
        Com_ShutdownZoneMemory();
        Com_ShutdownHunkMemory();
#endif

#ifdef _XBOX
	{
		extern void ERR_SetDiscFailReason(int);
		extern void ERR_DiscFail(bool);
		if (strstr(text, "GOB"))
		{
			ERR_SetDiscFailReason(1);
		}
		else if (strstr(text, "Stream") || strstr(text, "sound"))
		{
			ERR_SetDiscFailReason(3);
		}
		else
		{
			ERR_SetDiscFailReason(2);
		}
		ERR_DiscFail(false);
	}
#endif

        exit (1);
}


/*
================
Sys_GetEvent

================
*/
#define MAX_POLL_RATE	15
sysEvent_t Sys_GetEvent( void ) {
    sysEvent_t        ev;

	// return if we have data
	if(ClientManager::splitScreenMode == qtrue)
	{
		if(ClientManager::ActiveClient().eventHead > ClientManager::ActiveClient().eventTail ) {
			ClientManager::ActiveClient().eventTail++;
			return ClientManager::ActiveClient().eventQue[ (ClientManager::ActiveClient().eventTail - 1) & MASK_QUED_EVENTS ];
		}
	}
	else
	{
		if ( eventHead > eventTail ) {
            eventTail++;
       	    return eventQue[ ( eventTail - 1 ) & MASK_QUED_EVENTS ];
	    }
	}

    // check for network packets
	msg_t                netmsg;
	netadr_t	adr;

	MSG_Init( &netmsg, sys_packetReceived, sizeof( sys_packetReceived ) );
	if ( Sys_GetPacket ( &adr, &netmsg ) ) {
		netadr_t		*buf;
		int				len;

		// copy out to a seperate buffer for qeueing
		// the readcount stepahead is for SOCKS support
		len = sizeof( netadr_t ) + netmsg.cursize - netmsg.readcount;
		buf = (netadr_t *) Z_Malloc(len, TAG_EVENT, qfalse, 4);
		*buf = adr;
		memcpy( buf+1, &netmsg.data[netmsg.readcount], netmsg.cursize - netmsg.readcount );
		Sys_QueEvent( 0, SE_PACKET, 0, 0, len, buf );
	}

	//Check for broadcast messages
	MSG_Init( &netmsg, sys_packetReceived, sizeof( sys_packetReceived ) );
	if ( Sys_GetBroadcastPacket ( &netmsg ) )
	{
		// copy out to a seperate buffer for qeueing
		int len = netmsg.cursize - netmsg.readcount;
		char *buf = (char *) Z_Malloc(len, TAG_EVENT, qfalse, 4);
		memcpy( buf, &netmsg.data[netmsg.readcount], netmsg.cursize - netmsg.readcount );
		Sys_QueEvent( 0, SE_BROADCAST_PACKET, 0, 0, len, buf );
	}

	// return if we have data
	if(ClientManager::splitScreenMode == qtrue)
	{
		if(ClientManager::ActiveClient().eventHead > ClientManager::ActiveClient().eventTail ) {
			ClientManager::ActiveClient().eventTail++;
			return ClientManager::ActiveClient().eventQue[ (ClientManager::ActiveClient().eventTail - 1) & MASK_QUED_EVENTS ];
		}
	}
	else
	{
	    if ( eventHead > eventTail ) {
			eventTail++;
			return eventQue[ ( eventTail - 1 ) & MASK_QUED_EVENTS ];
    	}
	}

	// create an empty event to return
    memset( &ev, 0, sizeof( ev ) );
    ev.evTime = Sys_Milliseconds();

    return ev;
}


void Sys_Print(const char *msg)
{
#ifdef _GAMECUBE
	printf(msg);
#else
#if defined(STEFX_ELITE_FORCE_MP) && defined(_XBOX)
	(void)msg;
#else
	OutputDebugString(msg);
#endif
#endif
}

/*
==============
Sys_Log
==============
*/
void Sys_Log( const char *file, const char *msg ) {
	Sys_Log(file, msg, strlen(msg), strchr(msg, '\n') ? true : false);
}

/*
==============
Sys_Log
==============
*/
void Sys_Log( const char *file, const void *buffer, int size, bool flush ) {
#ifndef FINAL_BUILD
	static bool unableToLog = false;

	// Once we've failed to write to the log files once, bail out.
	// This lets us put release builds on DVD without recompiling.
	if (unableToLog)
		return;

	struct FileInfo
	{
		char name[MAX_QPATH];
		FILE *handle;
	};

	const int LOG_MAX_FILES = 4;
	static FileInfo files[LOG_MAX_FILES];
	static int num_files = 0;

	FileInfo* cur = NULL;
	for (int f = 0; f < num_files; ++f)
	{
		if (!stricmp(file, files[f].name))
		{
			cur = &files[f];
			break;
		}
	}

	if (cur == NULL)
	{
		if (num_files >= LOG_MAX_FILES)
		{
			Sys_Print("Too many log files!\n");
			return;
		}

		cur = &files[num_files++];
		strcpy(cur->name, file);
		cur->handle = NULL;
	}

	char fullname[MAX_QPATH];
	sprintf(fullname, "d:\\%s", cur->name);
	if (!cur->handle)
	{
		cur->handle = fopen(fullname, "wb");
		if (cur->handle == NULL)
		{
			Sys_Print("Unable to open log file!\n");
			unableToLog = true;
			return;
		}
	}

	if (size == 1) fputc(*(char*)buffer, cur->handle);
	else fwrite(buffer, size, 1, cur->handle);

	if (flush)
	{
		fflush(cur->handle);
	}
#endif
}

#ifdef _XBOX
HANDLE Sys_FileStreamMutex = INVALID_HANDLE_VALUE;
#endif

void Win_Init(void)
{
#ifdef _XBOX
	Sys_FileStreamMutex = CreateMutex(NULL, FALSE, NULL);
#endif
}

/*

Crappy full-screen texture drawing code from SP

*/

/*********
SP_DrawTexture
*********/
void SP_DrawTexture(void* pixels, float width, float height, float vShift)
{
	if (!pixels)
	{
		// Ug.  We were not even able to load the error message texture.
		return;
	}
	
	// Create a texture from the buffered file
	GLuint texid;
	qglGenTextures(1, &texid);
	qglBindTexture(GL_TEXTURE_2D, texid);
	qglTexImage2D(GL_TEXTURE_2D, 0, GL_DDS1_EXT, width, height, 0, GL_DDS1_EXT, GL_UNSIGNED_BYTE, pixels);

	qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP );
	qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP );

	// Reset every GL state we've got.  Who knows what state
	// the renderer could be in when this function gets called.
	qglColor3f(1.f, 1.f, 1.f);
#ifdef _XBOX
	if(glw_state->isWidescreen)
		qglViewport(0, 0, 720, 480);
	else
#endif
	qglViewport(0, 0, 640, 480);

	GLboolean alpha = qglIsEnabled(GL_ALPHA_TEST);
	qglDisable(GL_ALPHA_TEST);

	GLboolean blend = qglIsEnabled(GL_BLEND);
	qglDisable(GL_BLEND);

	GLboolean cull = qglIsEnabled(GL_CULL_FACE);
	qglDisable(GL_CULL_FACE);

	GLboolean depth = qglIsEnabled(GL_DEPTH_TEST);
	qglDisable(GL_DEPTH_TEST);

	GLboolean fog = qglIsEnabled(GL_FOG);
	qglDisable(GL_FOG);

	GLboolean lighting = qglIsEnabled(GL_LIGHTING);
	qglDisable(GL_LIGHTING);

	GLboolean offset = qglIsEnabled(GL_POLYGON_OFFSET_FILL);
	qglDisable(GL_POLYGON_OFFSET_FILL);

	GLboolean scissor = qglIsEnabled(GL_SCISSOR_TEST);
	qglDisable(GL_SCISSOR_TEST);

	GLboolean stencil = qglIsEnabled(GL_STENCIL_TEST);
	qglDisable(GL_STENCIL_TEST);

	GLboolean texture = qglIsEnabled(GL_TEXTURE_2D);
	qglEnable(GL_TEXTURE_2D);

	qglMatrixMode(GL_MODELVIEW);
	qglLoadIdentity();
	qglMatrixMode(GL_PROJECTION);
	qglLoadIdentity();
#ifdef _XBOX
	if(glw_state->isWidescreen)
        qglOrtho(0, 720, 0, 480, 0, 1);
	else
#endif
	qglOrtho(0, 640, 0, 480, 0, 1);
	
	qglMatrixMode(GL_TEXTURE0);
	qglLoadIdentity();
	qglMatrixMode(GL_TEXTURE1);
	qglLoadIdentity();

	qglActiveTextureARB(GL_TEXTURE0_ARB);
	qglClientActiveTextureARB(GL_TEXTURE0_ARB);

	memset(&tess, 0, sizeof(tess));

	// Draw the error message
	qglBeginFrame();

/*	if (!SP_LicenseDone)
	{
		// clear the screen if we haven't done the
		// license yet...
		qglClearColor(0, 0, 0, 1);
		qglClear(GL_COLOR_BUFFER_BIT);
	}
*/
	float x1, x2, y1, y2;
#ifdef _XBOX
	if(glw_state->isWidescreen)
	{
		x1 = 0;
		x2 = 720;
		y1 = 0;
		y2 = 480;
	}
	else {
#endif
	x1 = 0;
	x2 = 640;
	y1 = 0;
	y2 = 480;
#ifdef _XBOX
	}
#endif

	y1 += vShift;
	y2 += vShift;

	qglBeginEXT (GL_TRIANGLE_STRIP, 4, 0, 0, 4, 0);
		qglTexCoord2f( 0,  0 );
		qglVertex2f(x1, y1);
		qglTexCoord2f( 1 ,  0 );
		qglVertex2f(x2, y1);
		qglTexCoord2f( 0, 1 );
		qglVertex2f(x1, y2);
		qglTexCoord2f( 1, 1 );
		qglVertex2f(x2, y2);
	qglEnd();
	
	qglEndFrame();
	qglFlush();

	// Restore (most) of the render states we reset
	if (alpha) qglEnable(GL_ALPHA_TEST);
	else qglDisable(GL_ALPHA_TEST);

	if (blend) qglEnable(GL_BLEND);
	else qglDisable(GL_BLEND);

	if (cull) qglEnable(GL_CULL_FACE);
	else qglDisable(GL_CULL_FACE);

	if (depth) qglEnable(GL_DEPTH_TEST);
	else qglDisable(GL_DEPTH_TEST);

	if (fog) qglEnable(GL_FOG);
	else qglDisable(GL_FOG);

	if (lighting) qglEnable(GL_LIGHTING);
	else qglDisable(GL_LIGHTING);

	if (offset) qglEnable(GL_POLYGON_OFFSET_FILL);
	else qglDisable(GL_POLYGON_OFFSET_FILL);

	if (scissor) qglEnable(GL_SCISSOR_TEST);
	else qglDisable(GL_SCISSOR_TEST);

	if (stencil) qglEnable(GL_STENCIL_TEST);
	else qglDisable(GL_STENCIL_TEST);

	if (texture) qglEnable(GL_TEXTURE_2D);
	else qglDisable(GL_TEXTURE_2D);

	// Kill the texture
	qglDeleteTextures(1, &texid);
}


/*********
SP_GetLanguageExt

Retuns the extension for the current language, or
english if the language is unknown.
*********/
char* SP_GetLanguageExt()
{
	switch(XGetLanguage())
	{
	case XC_LANGUAGE_ENGLISH:
		return "EN";
//	case XC_LANGUAGE_JAPANESE:
//		return "JA";
	case XC_LANGUAGE_GERMAN:
		return "GE";
//	case XC_LANGUAGE_SPANISH:
//		return "SP";
//	case XC_LANGUAGE_ITALIAN:
//		return "IT";
//	case XC_LANGUAGE_KOREAN:
//		return "KO";
//	case XC_LANGUAGE_TCHINESE:
//		return "CH";
//	case XC_LANGUAGE_PORTUGUESE:
//		return "PO";
	case XC_LANGUAGE_FRENCH:
		return "FR";
	default:
		return "EN";
	}
}

/*********
SP_LoadFile
*********/
void* SP_LoadFile(const char* name)
{
	wfhandle_t h = WF_Open(name, true, false);
	if (h < 0) return NULL;

	if (WF_Seek(0, SEEK_END, h))
	{
		WF_Close(h);
		return NULL;
	}

	int len = WF_Tell(h);
	
	if (WF_Seek(0, SEEK_SET, h))
	{
		WF_Close(h);
		return NULL;
	}

	void *buf = Z_Malloc(len, TAG_TEMP_WORKSPACE, false, 32);

	if (WF_Read(buf, len, h) != len)
	{
		Z_Free(buf);
		WF_Close(h);
		return NULL;
	}

	WF_Close(h);

	return buf;
}

/*********
SP_LoadFileWithLanguage

Loads a screen with the appropriate language
*********/
void *SP_LoadFileWithLanguage(const char *name)
{
	char fullname[MAX_QPATH];
	void *buffer = NULL;
	char *ext;

	// get the language extension
	ext = SP_GetLanguageExt();

	// creat the fullpath name and try to load the texture
	sprintf(fullname, "%s_%s.dds", name, ext);
	buffer = SP_LoadFile(fullname);

	if (!buffer)
	{
		sprintf(fullname, "%s.dds", name);
		buffer = SP_LoadFile(fullname);
	}

	return buffer;
}

/*
SP_DrawSPLoadScreen

Draws the single player loading screen - used when skipping the logo movies
*/
void SP_DrawSPLoadScreen( void )
{
	// Load the texture:
	void *image = SP_LoadFileWithLanguage("d:\\base\\media\\LoadSP");

	if( image )
	{
		SP_DrawTexture(image, 512, 512, 0);
		Z_Free(image);
	}
}

/*
ERR_DiscFail

Draws the damaged/dirty disc message, looping forever
*/
static int s_discFailReason = 0;

void ERR_SetDiscFailReason(int reason)
{
	s_discFailReason = reason;
}

void ERR_DiscFail(bool poll)
{
	// Load the texture:
	const char *screenName = "d:\\base\\media\\DiscErr";
	if (s_discFailReason == 1)
	{
		screenName = "d:\\base\\media\\LoadMP";
	}
	else if (s_discFailReason == 2)
	{
		screenName = "d:\\base\\media\\LoadSP";
	}
	else if (s_discFailReason == 3)
	{
		screenName = "d:\\base\\media\\LicenseScreen";
	}

	void *image = SP_LoadFileWithLanguage(screenName);

	if( image )
	{
		SP_DrawTexture(image, 512, 512, 0);
		Z_Free(image);
	}

	for (;;)
	{
		extern void S_Update_(void);
		S_Update_();
	}
}

/*****************************************************************************/

/*
=====================

XBE SWITCHING SUPPORT

=====================
*/
#define LAUNCH_MAGIC "J3D1"
void Sys_Reboot( const char *reason )
{
	LAUNCH_DATA ld;
	const char *path = NULL;

	memset( &ld, 0, sizeof(ld) );

	if (!Q_stricmp(reason, "new_account"))
	{
		PLD_LAUNCH_DASHBOARD pDash	= (PLD_LAUNCH_DASHBOARD) &ld;
		pDash->dwReason				= XLD_LAUNCH_DASHBOARD_NEW_ACCOUNT_SIGNUP;
		path						= NULL;
	}
	else if (!Q_stricmp(reason, "net_config"))
	{
		PLD_LAUNCH_DASHBOARD pDash	= (PLD_LAUNCH_DASHBOARD) &ld;
		pDash->dwReason				= XLD_LAUNCH_DASHBOARD_NETWORK_CONFIGURATION;
		path						= NULL;
	}
	else if (!Q_stricmp(reason, "manage_account"))
	{
		PLD_LAUNCH_DASHBOARD pDash	= (PLD_LAUNCH_DASHBOARD) &ld;
		pDash->dwReason				= XLD_LAUNCH_DASHBOARD_ACCOUNT_MANAGEMENT;
		path						= NULL;
	}
	else if (!Q_stricmp(reason, "singleplayer"))
	{
		SP_DrawSPLoadScreen();
		glw_state->device->PersistDisplay();
		path = "d:\\default.xbe";
		ld.Data[0] = IN_GetMainController();
		strcpy((char *)&ld.Data[1], LAUNCH_MAGIC);
		if( Settings.IsDisabled() )
			ld.Data[5] = 0x42;
	}
	else
	{
		Com_Error( ERR_FATAL, "Unknown reboot code %s\n", reason );
	}

	// Title should not be doing ANYTHING in the background.
	// Shutting down sound ensures that the sound thread is gone
	S_Shutdown();
	// Similarly, kill off the streaming thread
	extern void Sys_StreamShutdown(void);
	Sys_StreamShutdown();

	XLaunchNewImage(path, &ld);

	// This function should not return!
	Com_Error( ERR_FATAL, "ERROR: XLaunchNewImage returned\n" );
}

static LAUNCH_DATA s_ld;

// Run-once function to make sure that ld is filled in.
// Call this from any function that needs to use s_ld:
static void _initLD( void )
{
	static bool initialized = false;

	if( !initialized )
	{
		initialized = true;

		DWORD launchType;
		if( XGetLaunchInfo( &launchType, &s_ld ) != ERROR_SUCCESS ||
			launchType != LDT_TITLE )
			memset( &s_ld, 0, sizeof(s_ld) );

		if( s_ld.Data[1] == 0x42 )
			Settings.Disable();
	}
}

int Sys_GetLaunchController( void )
{
	_initLD();

	return s_ld.Data[0];
}

// Used to check for the presence of an accepted invite for our game on the HD.
// This actually looks in the launch_data, because the SP game absorbs it, then
// copies it back to the LD before rebooting. Bleh.
XONLINE_ACCEPTED_GAMEINVITE *Sys_AcceptedInvite( void )
{
	_initLD();

	// Flag to indicate whether or not we had an invite:
	if( !s_ld.Data[2] )
		return NULL;

	// OK. The SP XBE should have just copied the invite to the LD:
	return (XONLINE_ACCEPTED_GAMEINVITE *) &s_ld.Data[3];
}

static void JAMP_LogSEHValue( const char *label, unsigned int value )
{
	char msg[128];
	_snprintf( msg, sizeof( msg ), "JAMP: SEH %s=0x%08X", label, value );
	msg[sizeof( msg ) - 1] = 0;
	XBLog_Write( msg );
}

static int JAMP_LogSEHException( const char *phase, EXCEPTION_POINTERS *exceptionInfo )
{
	char msg[160];
	_snprintf( msg, sizeof( msg ), "JAMP: SEH exception in %s", phase );
	msg[sizeof( msg ) - 1] = 0;
	XBLog_Write( msg );

	if ( exceptionInfo && exceptionInfo->ExceptionRecord )
	{
		JAMP_LogSEHValue( "code", (unsigned int)exceptionInfo->ExceptionRecord->ExceptionCode );
		JAMP_LogSEHValue( "address", (unsigned int)exceptionInfo->ExceptionRecord->ExceptionAddress );
	}

#if defined(_M_IX86)
	if ( exceptionInfo && exceptionInfo->ContextRecord )
	{
		CONTEXT *context = exceptionInfo->ContextRecord;
		JAMP_LogSEHValue( "EIP", (unsigned int)context->Eip );
		JAMP_LogSEHValue( "EAX", (unsigned int)context->Eax );
		JAMP_LogSEHValue( "EBX", (unsigned int)context->Ebx );
		JAMP_LogSEHValue( "ECX", (unsigned int)context->Ecx );
		JAMP_LogSEHValue( "EDX", (unsigned int)context->Edx );
		JAMP_LogSEHValue( "ESI", (unsigned int)context->Esi );
		JAMP_LogSEHValue( "EDI", (unsigned int)context->Edi );
		JAMP_LogSEHValue( "ESP", (unsigned int)context->Esp );
		JAMP_LogSEHValue( "EBP", (unsigned int)context->Ebp );
	}
#endif

	return EXCEPTION_EXECUTE_HANDLER;
}

/*
==================
WinMain

==================
*/
#if defined (_XBOX)
int __cdecl main()
#elif defined (_GAMECUBE)
int main(int argc, char* argv[])
#endif
{
	// I'm going to kill someone. This should not be necessary. No, really.
	XBLog_MainProbe();
	OutputDebugStringA("JAMP: main() entered\n");

	{
		XDEVICE_PREALLOC_TYPE xdpt[2];
		xdpt[0].DeviceType      = XDEVICE_TYPE_GAMEPAD;
		xdpt[0].dwPreallocCount = 4;
		xdpt[1].DeviceType      = XDEVICE_TYPE_MEMORY_UNIT;
		xdpt[1].dwPreallocCount = 8;

		XBLog_StartupProbe("before SP-style XInitDevices");
		XBLog_Write("STEFX_HM: input Plan-B calling XInitDevices before D3D init");
		XInitDevices(2, xdpt);
		XBLog_Write("STEFX_HM: input Plan-B XInitDevices completed before D3D init");
		XBLog_StartupProbe("after SP-style XInitDevices");
	}
	{
		extern bool g_XInitDevicesAlreadyCalled;
		g_XInitDevicesAlreadyCalled = true;
	}

	XBLog_Write("STEFX_HM: MP using SP-style fakegl pushbuffer path; main skipped legacy Direct3D_SetPushBufferSize");

	// get the initial time base
	OutputDebugStringA("JAMP: Sys_Milliseconds...\n");
	XBLog_StartupProbe("before Sys_Milliseconds");
	Sys_Milliseconds();
	XBLog_StartupProbe("after Sys_Milliseconds");

	OutputDebugStringA("JAMP: Win_Init...\n");
	XBLog_StartupProbe("before Win_Init");
	Win_Init();
	XBLog_StartupProbe("after Win_Init");

	OutputDebugStringA("JAMP: XBLog_Init...\n");
	XBLog_StartupProbe("before XBLog_Init");
	XBLog_Init();
	XBLog_Write("JAMP: XBLog_Init done - log file open");

	XBLog_Write("JAMP: Com_Init starting...");
	XBLog_Write("STEFX_HM: startup command " JAMP_CXBX_SMOKE_STARTUP_COMMAND);
#if STEFX_HOLOMATCH_DIRECT_BOOT
	XBLog_Write("STEFX_HM: direct Holomatch startup bypasses menus; loading hm_borg1 from command line");
#endif
	static char jampStartupCommand[] = JAMP_CXBX_SMOKE_STARTUP_COMMAND;
	Com_Init( jampStartupCommand );
	XBLog_Write("JAMP: Com_Init done");

	//Start sound early.  The STL inside will allocate memory and we don't
	//want that memory in the middle of the zone.
	XBLog_Write("JAMP: S_BeginRegistration...");
	if ( !cls.soundRegistered ) {
		cls.soundRegistered = qtrue;
		S_BeginRegistration(ClientManager::NumClients());
	}
	XBLog_Write("JAMP: S_BeginRegistration done");

//	NET_Init();
	// At this point, we NEED our local address:
	XBLog_Write("JAMP: NET_GetLocalAddress...");
	extern void NET_GetLocalAddress( bool force );
	NET_GetLocalAddress( true );
	XBLog_Write("JAMP: NET_GetLocalAddress done");

	// A sample does this, seems un-necessary though:
//	XNetGetBroadcastVersionStatus( TRUE );

	// Check for a pending invitation on the HD. We call this now to
	// force the result to be retrieved and cached inside the func.
	XBLog_Write("JAMP: Sys_AcceptedInvite...");
	Sys_AcceptedInvite();
	XBLog_Write("JAMP: Entering main game loop");

	// main game loop
	int jampFrameHeartbeat = 0;
	while( 1 ) {
		/*
		extern void PrintMem(void);
		PrintMem();
		*/
		qboolean jampLoopTrace = (jampFrameHeartbeat < 2);
#if defined(STEFX_ELITE_FORCE_MP)
		{
			static int stefxHmMainLoopTraceBudget = 16;
			const char *stefxTraceMap = Cvar_VariableString( "mapname" );
			qboolean stefxHmMainLoopTrace = (qboolean)(
				stefxHmMainLoopTraceBudget > 0 &&
				com_sv_running &&
				com_sv_running->integer &&
				stefxTraceMap &&
				!Q_stricmp( stefxTraceMap, "hm_borg1" ) );
			if ( stefxHmMainLoopTrace )
			{
				stefxHmMainLoopTraceBudget--;
				jampLoopTrace = qtrue;
			}
		}
#endif
		XBLog_Phase("main loop before IN_Frame");
		if (jampLoopTrace)
		{
			char traceMsg[96];
			_snprintf(traceMsg, sizeof(traceMsg), "JAMP: main loop frame=%i before IN_Frame", jampFrameHeartbeat);
			traceMsg[sizeof(traceMsg) - 1] = 0;
			XBLog_Write(traceMsg);
		}
#if JAMP_USE_MAINLOOP_SEH
		__try
		{
			IN_Frame();
		}
		__except( JAMP_LogSEHException( "IN_Frame", GetExceptionInformation() ) )
		{
			return 1;
		}
#else
		IN_Frame();
#endif
		XBLog_Phase("main loop after IN_Frame");
		if (jampLoopTrace)
		{
			char traceMsg[96];
			_snprintf(traceMsg, sizeof(traceMsg), "JAMP: main loop frame=%i after IN_Frame", jampFrameHeartbeat);
			traceMsg[sizeof(traceMsg) - 1] = 0;
			XBLog_Write(traceMsg);
		}
		if (jampLoopTrace)
		{
			char traceMsg[96];
			_snprintf(traceMsg, sizeof(traceMsg), "JAMP: main loop frame=%i before Com_Frame", jampFrameHeartbeat);
			traceMsg[sizeof(traceMsg) - 1] = 0;
			XBLog_Write(traceMsg);
		}
		XBLog_Phase("main loop before Com_Frame");
#if JAMP_USE_MAINLOOP_SEH
		__try
		{
			Com_Frame();
		}
		__except( JAMP_LogSEHException( "Com_Frame", GetExceptionInformation() ) )
		{
			return 1;
		}
#else
		Com_Frame();
#endif
		XBLog_Phase("main loop after Com_Frame");
		if (jampLoopTrace)
		{
			char traceMsg[96];
			_snprintf(traceMsg, sizeof(traceMsg), "JAMP: main loop frame=%i after Com_Frame", jampFrameHeartbeat);
			traceMsg[sizeof(traceMsg) - 1] = 0;
			XBLog_Write(traceMsg);
		}
		jampFrameHeartbeat++;

#ifdef _XBOX
		{
			static int stefxMainLoopHeartbeatTime = 0;
			const int stefxNow = Sys_Milliseconds();
			if ( stefxNow < stefxMainLoopHeartbeatTime ||
				stefxNow - stefxMainLoopHeartbeatTime >= 3000 )
			{
				char traceMsg[192];
				const char *heartbeatMap = Cvar_VariableString( "mapname" );
				_snprintf( traceMsg, sizeof( traceMsg ),
					"STEFX_HM: main loop heartbeat frame=%i time=%d state=%i sv=%i map='%s'",
					jampFrameHeartbeat,
					stefxNow,
					cls.state,
					com_sv_running ? com_sv_running->integer : 0,
					heartbeatMap ? heartbeatMap : "" );
				traceMsg[sizeof(traceMsg) - 1] = 0;
				XBLog_Write( traceMsg );
				stefxMainLoopHeartbeatTime = stefxNow;
			}
		}
#endif

#if STEFX_HOLOMATCH_DIRECT_BOOT
		static qboolean stefxHolomatchDirectMapQueued = qfalse;
		static qboolean stefxHolomatchDirectBotsQueued = qfalse;
		static qboolean stefxHolomatchDirectBotsWaitLogged = qfalse;
		static qboolean stefxHolomatchDirectBotsMapLogged = qfalse;
		static qboolean stefxHolomatchDirectClientActiveLogged = qfalse;
		static qboolean stefxHolomatchDirectClientWaitLogged = qfalse;
		static int stefxHolomatchDirectMapFrames = 0;
		static int stefxHolomatchDirectClientFrames = 0;
		const char *directMap = Cvar_VariableString( "mapname" );
		qboolean directMapRunning = (qboolean)( com_sv_running &&
			com_sv_running->integer &&
			directMap &&
			!Q_stricmp( directMap, "hm_borg1" ) );

		if ( !stefxHolomatchDirectMapQueued &&
			!directMapRunning &&
			jampFrameHeartbeat >= STEFX_HOLOMATCH_DIRECT_MAP_FRAME )
		{
			char traceMsg[160];
			_snprintf( traceMsg, sizeof( traceMsg ),
				"STEFX_HM: queueing direct Holomatch map frame=%i sv=%i map='%s'",
				jampFrameHeartbeat,
				com_sv_running ? com_sv_running->integer : 0,
				directMap ? directMap : "" );
			traceMsg[sizeof(traceMsg) - 1] = 0;
			XBLog_Write( traceMsg );
			XBLog_Write( "STEFX_HM: direct Holomatch render debug cvars forced off before map" );
			Cbuf_AddText( STEFX_HOLOMATCH_DIRECT_RENDER_CVAR_COMMAND );
			Cbuf_AddText( STEFX_HOLOMATCH_DIRECT_MAP_COMMAND );
			stefxHolomatchDirectMapQueued = qtrue;
		}

		if ( !stefxHolomatchDirectBotsQueued )
		{
			if ( directMapRunning )
			{
				stefxHolomatchDirectMapFrames++;
				if ( cls.state == CA_ACTIVE )
				{
					stefxHolomatchDirectClientFrames++;
					if ( !stefxHolomatchDirectClientActiveLogged )
					{
						char traceMsg[160];
						_snprintf( traceMsg, sizeof( traceMsg ),
							"STEFX_HM: direct Holomatch local client is active frame=%i state=%i mapFrames=%i",
							jampFrameHeartbeat, cls.state, stefxHolomatchDirectMapFrames );
						traceMsg[sizeof(traceMsg) - 1] = 0;
						XBLog_Write( traceMsg );
						stefxHolomatchDirectClientActiveLogged = qtrue;
					}
				}
				else
				{
					stefxHolomatchDirectClientFrames = 0;
					if ( !stefxHolomatchDirectClientWaitLogged &&
						jampFrameHeartbeat >= STEFX_HOLOMATCH_DIRECT_BOT_FRAME )
					{
						char traceMsg[160];
						_snprintf( traceMsg, sizeof( traceMsg ),
							"STEFX_HM: waiting for direct Holomatch local client before bots frame=%i state=%i mapFrames=%i",
							jampFrameHeartbeat, cls.state, stefxHolomatchDirectMapFrames );
						traceMsg[sizeof(traceMsg) - 1] = 0;
						XBLog_Write( traceMsg );
						stefxHolomatchDirectClientWaitLogged = qtrue;
					}
				}

				if ( !stefxHolomatchDirectBotsMapLogged )
				{
					char traceMsg[128];
					_snprintf( traceMsg, sizeof( traceMsg ),
						"STEFX_HM: direct Holomatch map is running map='%s' frame=%i",
						directMap, jampFrameHeartbeat );
					traceMsg[sizeof(traceMsg) - 1] = 0;
					XBLog_Write( traceMsg );
					stefxHolomatchDirectBotsMapLogged = qtrue;
				}

				if ( jampFrameHeartbeat >= STEFX_HOLOMATCH_DIRECT_BOT_FRAME &&
					stefxHolomatchDirectMapFrames >= STEFX_HOLOMATCH_DIRECT_BOT_STABLE_FRAMES )
				{
					char traceMsg[160];
					_snprintf( traceMsg, sizeof( traceMsg ),
						"STEFX_HM: queueing direct Holomatch bots frame=%i state=%i mapFrames=%i clientFrames=%i",
						jampFrameHeartbeat, cls.state, stefxHolomatchDirectMapFrames, stefxHolomatchDirectClientFrames );
					traceMsg[sizeof(traceMsg) - 1] = 0;
					XBLog_Write( traceMsg );
					Cbuf_AddText( STEFX_HOLOMATCH_DIRECT_BOT_COMMAND );
					stefxHolomatchDirectBotsQueued = qtrue;
				}
			}
			else if ( !stefxHolomatchDirectBotsWaitLogged &&
				jampFrameHeartbeat >= STEFX_HOLOMATCH_DIRECT_BOT_FRAME )
			{
				char traceMsg[128];
				_snprintf( traceMsg, sizeof( traceMsg ),
					"STEFX_HM: waiting for direct Holomatch map before bots sv=%i map='%s'",
					com_sv_running ? com_sv_running->integer : 0,
					directMap ? directMap : "" );
				traceMsg[sizeof(traceMsg) - 1] = 0;
				XBLog_Write( traceMsg );
				stefxHolomatchDirectBotsWaitLogged = qtrue;
			}
		}
#endif

#if JAMP_XEMU_DIRECT_MATCH
		static qboolean jampDirectMatchQueued = qfalse;
		if ( !jampDirectMatchQueued && jampFrameHeartbeat == 8 )
		{
			XBLog_Write("JAMP: queueing direct Xemu local match");
			Cbuf_AddText( JAMP_XEMU_DIRECT_MATCH_COMMAND );
			jampDirectMatchQueued = qtrue;
		}
#endif

		// Do any XBL stuff. Original Xbox JAMP left this disabled from the
		// shell loop; keep the default matching that until Live/System Link is
		// audited separately.
#if JAMP_ENABLE_MAINLOOP_XBL_TICK
		XBLog_Phase("main loop before XBL_Tick");
		if (jampLoopTrace)
		{
			char traceMsg[96];
			_snprintf(traceMsg, sizeof(traceMsg), "JAMP: main loop frame=%i before XBL_Tick", jampFrameHeartbeat - 1);
			traceMsg[sizeof(traceMsg) - 1] = 0;
			XBLog_Write(traceMsg);
		}
#if JAMP_USE_MAINLOOP_SEH
		__try
		{
			XBL_Tick();
		}
		__except( JAMP_LogSEHException( "XBL_Tick", GetExceptionInformation() ) )
		{
			return 1;
		}
#else
		XBL_Tick();
#endif
		XBLog_Phase("main loop after XBL_Tick");
		if (jampLoopTrace)
		{
			char traceMsg[96];
			_snprintf(traceMsg, sizeof(traceMsg), "JAMP: main loop frame=%i after XBL_Tick", jampFrameHeartbeat - 1);
			traceMsg[sizeof(traceMsg) - 1] = 0;
			XBLog_Write(traceMsg);
		}
#else
		static int jampSkippedXBLTick = 0;
		if ( !jampSkippedXBLTick )
		{
			XBLog_Phase("main loop XBL_Tick disabled");
			XBLog_Write("JAMP: XBL_Tick skipped in main loop");
			jampSkippedXBLTick = 1;
		}
#endif

		// Poll debug console for new commands
#ifndef FINAL_BUILD
#if defined(_XBOX)
		// Retail Xbox testing has no debug monitor channel. Cxbx-R also crashes
		// in this path after the game loop is already healthy, so keep logging
		// as the diagnostic channel and leave XBDM command polling disabled.
		static int jampSkippedDebugConsole = 0;
		if ( !jampSkippedDebugConsole )
		{
			XBLog_Write("JAMP: DebugConsoleHandleCommands skipped on Xbox");
			jampSkippedDebugConsole = 1;
		}
#else
		if (jampFrameHeartbeat < 5 || !(jampFrameHeartbeat & 511))
		{
			XBLog_Write("JAMP: main loop before DebugConsoleHandleCommands");
		}
#if JAMP_USE_MAINLOOP_SEH
		__try
		{
			DebugConsoleHandleCommands();
		}
		__except( JAMP_LogSEHException( "DebugConsoleHandleCommands", GetExceptionInformation() ) )
		{
			return 1;
		}
#else
		DebugConsoleHandleCommands();
#endif
		if (jampFrameHeartbeat < 5 || !(jampFrameHeartbeat & 511))
		{
			XBLog_Write("JAMP: main loop after DebugConsoleHandleCommands");
		}
#endif
#endif
	}

	return 0;
}


char *Sys_GetClipboardData(void) { return NULL; }

void Sys_StartProcess(char *, qboolean) {}

void Sys_OpenURL(char *, int) {}

void Sys_Quit(void) {}

void Sys_ShowConsole(int, int) {}

void Sys_Mkdir(const char *) {}

int Sys_LowPhysicalMemory(void) { return 0; }

void Sys_FreeFileList(char **filelist)
{
	// All strings in a file list are allocated at once, so we just need to
	// do two frees, one for strings, one for the pointers.
	if ( filelist )
	{
		if ( filelist[0] )
			Z_Free( filelist[0] );

		Z_Free( filelist );
	}
}

#if defined(STEFX_ELITE_FORCE_MP)
static qboolean Sys_STEFXMatchesListRequest( const WIN32_FIND_DATA *data, const char *extension, qboolean wantsubs )
{
	qboolean isDir;
	const char *dot;

	if ( !data || !data->cFileName[0] || !Q_stricmp( data->cFileName, "." ) || !Q_stricmp( data->cFileName, ".." ) )
	{
		return qfalse;
	}

	isDir = (data->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? qtrue : qfalse;
	if ( wantsubs || !Q_stricmp( extension, "dir" ) )
	{
		return isDir;
	}
	if ( isDir )
	{
		return qfalse;
	}
	if ( !extension || !extension[0] )
	{
		return qtrue;
	}

	dot = strrchr( data->cFileName, '.' );
	return (dot && !Q_stricmp( dot + 1, extension )) ? qtrue : qfalse;
}

static char **Sys_STEFXListLooseFiles( const char *directory, const char *extension, int *numfiles, qboolean wantsubs )
{
	WIN32_FIND_DATA data;
	HANDLE h;
	char *osDir;
	char search[MAX_OSPATH];
	int nfiles;
	int stringBytes;
	char **retList;
	char *stringPool;

	if ( numfiles )
	{
		*numfiles = 0;
	}
	if ( !directory || !directory[0] )
	{
		return NULL;
	}

	if ( strchr( directory, ':' ) || directory[0] == '\\' || directory[0] == '/' )
	{
		osDir = (char *)directory;
	}
	else
	{
		osDir = FS_BuildOSPath( directory );
	}

	Com_sprintf( search, sizeof( search ), "%s\\*.*", osDir );
	nfiles = 0;
	stringBytes = 0;
	h = FindFirstFile( search, &data );
	while ( h != INVALID_HANDLE_VALUE )
	{
		if ( Sys_STEFXMatchesListRequest( &data, extension, wantsubs ) )
		{
			nfiles++;
			stringBytes += strlen( data.cFileName ) + 1;
		}
		if ( !FindNextFile( h, &data ) )
		{
			FindClose( h );
			h = INVALID_HANDLE_VALUE;
		}
	}

	if ( nfiles <= 0 )
	{
		return NULL;
	}

	retList = (char **)Z_Malloc( (nfiles + 1) * sizeof( *retList ), TAG_LISTFILES, qfalse );
	stringPool = (char *)Z_Malloc( stringBytes, TAG_LISTFILES, qfalse );

	nfiles = 0;
	h = FindFirstFile( search, &data );
	while ( h != INVALID_HANDLE_VALUE )
	{
		if ( Sys_STEFXMatchesListRequest( &data, extension, wantsubs ) )
		{
			retList[nfiles++] = stringPool;
			strcpy( stringPool, data.cFileName );
			stringPool += strlen( data.cFileName ) + 1;
		}
		if ( !FindNextFile( h, &data ) )
		{
			FindClose( h );
			h = INVALID_HANDLE_VALUE;
		}
	}
	retList[nfiles] = NULL;
	if ( numfiles )
	{
		*numfiles = nfiles;
	}

	Com_Printf( "STEFX_HM: loose Sys_ListFiles path='%s' ext='%s' count=%d\n",
		directory,
		extension ? extension : "",
		nfiles );
	return retList;
}
#endif

#ifdef _JK2MP
char** Sys_ListFiles(const char *directory, const char *extension, char *filter, int *numfiles, qboolean wantsubs)
#else
char** Sys_ListFiles(const char *directory, const char *extension, int *numfiles, qboolean wantsubs)
#endif
{
#ifdef _JK2MP
	// MP has extra filter paramter. We don't support that.
	if (filter)
	{
		assert(!"Sys_ListFiles doesn't support filter on console!");
		return NULL;
	}
#endif

	// Hax0red console version of Sys_ListFiles. We mangle our arguments to get a standard filename
	// That file should exist, and contain the list of files that meet this search criteria.
	char	listFilename[MAX_OSPATH];
	char	*listFile, *curFile, *end;
	int		nfiles;
	char	**retList;

	// S00per hack
	if (strstr(directory, "d:\\base\\"))
		directory += 8;
#if defined(STEFX_ELITE_FORCE_MP)
	else if (!_strnicmp(directory, "d:\\baseef\\", 10) ||
		!_strnicmp(directory, "e:\\baseef\\", 10) ||
		!_strnicmp(directory, "t:\\baseef\\", 10))
	{
		Com_Printf( "STEFX_HM: Sys_ListFiles normalized BaseEF path '%s'\n", directory );
		directory += 10;
	}
#endif

	if (!extension)
	{
		extension = "";
	}
	else if (extension[0] == '/' && extension[1] == 0)
	{
		// Passing a slash as extension will find directories
		extension = "dir";
	}
	else if (extension[0] == '.')
	{
		// Skip over leading .
		extension++;
	}

	// Build our filename
	Com_sprintf(listFilename, sizeof(listFilename), "%s\\_console_%s_list_", directory, extension);
	if (FS_ReadFile( listFilename, (void**)&listFile ) <= 0)
	{
		if(listFile) {
			FS_FreeFile(listFile);
		}
#if defined(STEFX_ELITE_FORCE_MP)
		retList = Sys_STEFXListLooseFiles( directory, extension, numfiles, wantsubs );
		if ( retList )
		{
			return retList;
		}
		if ( !Q_stricmp( directory, "strings" ) &&
			( !Q_stricmp( extension, "dir" ) || !Q_stricmp( extension, "str" ) ) )
		{
			Com_Printf( "STEFX_HM: Sys_ListFiles strings probe empty ext='%s'\n", extension );
			if ( numfiles )
			{
				*numfiles = 0;
			}
			return NULL;
		}
#endif
#ifndef FINAL_BUILD
		Com_Printf( "WARNING: List file %s not found\n", listFilename );
#endif
		if (numfiles)
			*numfiles = 0;
		return NULL;
	}

	// Do a first pass to count number of files in the list
	nfiles = 0;
	curFile = listFile;
	while (true)
	{
		// Find end of line
		end = strchr(curFile, '\r');
		if (end)
		{
			// Should have a \n next -- skip them both
			end += 2;
		}
		else
		{
			end = strchr(curFile, '\n');
			if (end) end++;
			else end = curFile + strlen(curFile);
		}

		// Is the line empty?  If so, we're done.
		if (!curFile || !curFile[0]) break;
		++nfiles;

		// Advance to next line
		curFile = end;
	}

	// Fill in caller's pointer for number of files found
	if (numfiles) *numfiles = nfiles;

	// Did we find any files at all?
	if (nfiles == 0)
	{
		FS_FreeFile(listFile);
		return NULL;
	}

	// Allocate a file list, and quick string pool, but use LISTFILES
	retList = (char **) Z_Malloc( ( nfiles + 1 ) * sizeof( *retList ), TAG_LISTFILES, qfalse);
	// Our string pool is actually slightly too large, but it's temporary, and that's better
	// than slightly too small
	char *stringPool = (char *) Z_Malloc( strlen(listFile) + 1, TAG_LISTFILES, qfalse );

	// Now go through the list of files again, and fill in the list to be returned
	nfiles = 0;
	curFile = listFile;
	while (true)
	{
		// Find end of line
		end = strchr(curFile, '\r');
		if (end)
		{
			// Should have a \n next -- skip them both
			*end++ = '\0';
			*end++ = '\0';
		}
		else
		{
			end = strchr(curFile, '\n');
			if (end) *end++ = '\0';
			else end = curFile + strlen(curFile);
		}

		// Is the line empty?  If so, we're done.
		int curStrSize = strlen(curFile);
		if (curStrSize < 1)
		{
			retList[nfiles] = NULL;
			break;
		}

		// Alloc a small copy
		//retList[nfiles++] = CopyString( curFile );
		retList[nfiles++] = stringPool;
		strcpy(stringPool, curFile);
		stringPool += (curStrSize + 1);

		// Advance to next line
		curFile = end;
	}

	// Free the special file's buffer
	FS_FreeFile( listFile );

	return retList;
}

/*
=================
Sys_UnloadGame
=================
*/
void Sys_UnloadGame( void ) {
}

/*
=================
Sys_GetGameAPI

Loads the game dll
=================
*/
#ifndef _JK2MP
void *Sys_GetGameAPI (void *parms)
{
	extern game_export_t *GetGameAPI( game_import_t *import );
	return GetGameAPI((game_import_t *)parms);
}
#endif

/*
=================
Sys_LoadCgame

Used to hook up a development dll
=================
*/
// void * Sys_LoadCgame( void ) 
#ifndef _JK2MP
void * Sys_LoadCgame( int (**entryPoint)(int, ...), int (*systemcalls)(int, ...) )
{
	extern void CG_PreInit();
	extern void cg_dllEntry( int (*syscallptr)( int arg,... ) );
	extern int vmMain( int command, int arg0, int arg1, int arg2, int arg3, int arg4, int arg5, int arg6, int arg7 );
	cg_dllEntry(systemcalls);
	*entryPoint = (int (*)(int,...))vmMain;
	CG_PreInit();
	return 0;
}
#endif

/* VVFIXME: More stubs */
qboolean Sys_FileOutOfDate( LPCSTR psFinalFileName /* dest */, LPCSTR psDataFileName /* src */ )
{
	return qfalse;
}

qboolean Sys_CopyFile(LPCSTR lpExistingFileName, LPCSTR lpNewFileName, qboolean bOverwrite)
{
	return qfalse;
}

qboolean Sys_CheckCD( void )
{
	return qtrue;
}
