
#include "../client/client.h"
#include "../renderer/tr_local.h"
#include "../win32/glw_win_dx8.h"
#include "../win32/win_local.h"
#include "../win32/win_file.h"
#include "../ui/ui_splash.h"

#ifdef _XBOX
#include <xb_log.h>
#endif

extern bool Sys_QuickStart( void );

/*********
Globals
*********/
static bool SP_LicenseDone = false;

#ifdef _XBOX
static const char *s_spDrawTextureContext = "unknown";

static void SP_SetDrawTextureContext(const char *context)
{
	s_spDrawTextureContext = context ? context : "unknown";
}
#endif

/*********
SP_DisplayIntros
Draws intro movies to the screen
*********/
void SP_DisplayLogos(void)
{
	if( !Sys_QuickStart() )
		CIN_PlayAllFrames( "logos", 0, 0, 640, 480, 0, true );
}

/*********
SP_DrawTexture
*********/
void SP_DrawTexture(void* pixels, float width, float height, float vShift)
{
#ifdef _XBOX
	static int s_drawTextureCount = 0;
	bool logDetailed = (s_drawTextureCount < 3) || ((s_drawTextureCount & 63) == 0);
	{ char b[160]; _snprintf(b, sizeof(b), "SDT: entry #%d context=%s\n", s_drawTextureCount, s_spDrawTextureContext); b[sizeof(b)-1]=0; XBLog_Write(b); }
	s_drawTextureCount++;
#endif
	if (!pixels)
	{
		// Ug.  We were not even able to load the error message texture.
#ifdef _XBOX
		XBLog_Write("SDT: pixels NULL, return\n");
#endif
		return;
	}

	// Create a texture from the buffered file
	GLuint texid;
#ifdef _XBOX
	if (logDetailed) XBLog_Write("SDT: glGenTextures...\n");
#endif
	glGenTextures(1, &texid);
#ifdef _XBOX
	if (logDetailed) { char b[64]; _snprintf(b, sizeof(b), "SDT: glGenTextures -> texid=%u\n", (unsigned)texid); b[sizeof(b)-1]=0; XBLog_Write(b); }
	if (logDetailed) XBLog_Write("SDT: glBindTexture...\n");
#endif
	glBindTexture(GL_TEXTURE_2D, texid);
#ifdef _XBOX
	if (logDetailed) XBLog_Write("SDT: glTexImage2D (DDS1)...\n");
#endif
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DDS1_EXT, width, height, 0, GL_DDS1_EXT, GL_UNSIGNED_BYTE, pixels);
#ifdef _XBOX
	if (logDetailed) XBLog_Write("SDT: glTexParameterf x4...\n");
#endif

	glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP );
	glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP );

	// Reset every GL state we've got.  Who knows what state
	// the renderer could be in when this function gets called.
#ifdef _XBOX
	if (logDetailed) XBLog_Write("SDT: glColor3f...\n");
#endif
	glColor3f(1.f, 1.f, 1.f);
#ifdef _XBOX
	if (logDetailed) XBLog_Write("SDT: glViewport...\n");
	if(glw_state->isWidescreen)
		glViewport(0, 0, 720, 480);
	else
#endif
	glViewport(0, 0, 640, 480);
#ifdef _XBOX
	if (logDetailed) XBLog_Write("SDT: glViewport done\n");
#endif

#ifdef _XBOX
	if (logDetailed) XBLog_Write("SDT: glIsEnabled x10...\n");
#endif
	GLboolean alpha = glIsEnabled(GL_ALPHA_TEST);
	glDisable(GL_ALPHA_TEST);

	GLboolean blend = glIsEnabled(GL_BLEND);
	glDisable(GL_BLEND);

	GLboolean cull = glIsEnabled(GL_CULL_FACE);
	glDisable(GL_CULL_FACE);

	GLboolean depth = glIsEnabled(GL_DEPTH_TEST);
	glDisable(GL_DEPTH_TEST);

	GLboolean fog = glIsEnabled(GL_FOG);
	glDisable(GL_FOG);

	GLboolean lighting = glIsEnabled(GL_LIGHTING);
	glDisable(GL_LIGHTING);

	GLboolean offset = glIsEnabled(GL_POLYGON_OFFSET_FILL);
	glDisable(GL_POLYGON_OFFSET_FILL);

	GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
	glDisable(GL_SCISSOR_TEST);

	GLboolean stencil = glIsEnabled(GL_STENCIL_TEST);
	glDisable(GL_STENCIL_TEST);

	GLboolean texture = glIsEnabled(GL_TEXTURE_2D);
	glEnable(GL_TEXTURE_2D);

#ifdef _XBOX
	if (logDetailed) XBLog_Write("SDT: matrix setup (MV+PROJ+ortho)...\n");
#endif
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
#ifdef _XBOX
	if(glw_state->isWidescreen)
        glOrtho(0, 720, 0, 480, 0, 1);
	else
#endif
	glOrtho(0, 640, 0, 480, 0, 1);

#ifdef _XBOX
	if (logDetailed) XBLog_Write("SDT: glMatrixMode(GL_TEXTURE0) [non-std arg]...\n");
#endif
	glMatrixMode(GL_TEXTURE0);
	glLoadIdentity();
#ifdef _XBOX
	if (logDetailed) XBLog_Write("SDT: glMatrixMode(GL_TEXTURE1) [non-std arg]...\n");
#endif
	glMatrixMode(GL_TEXTURE1);
	glLoadIdentity();

#ifdef _XBOX
	if (logDetailed) XBLog_Write("SDT: glActiveTextureARB(GL_TEXTURE0_ARB)...\n");
#endif
	glActiveTextureARB(GL_TEXTURE0_ARB);
#ifdef _XBOX
	if (logDetailed) XBLog_Write("SDT: glClientActiveTextureARB(GL_TEXTURE0_ARB)...\n");
#endif
	glClientActiveTextureARB(GL_TEXTURE0_ARB);

#ifdef _XBOX
	if (logDetailed) XBLog_Write("SDT: memset(&tess)...\n");
#endif
	memset(&tess, 0, sizeof(tess));

	// Draw the error message
#ifdef _XBOX
	if (logDetailed) XBLog_Write("SDT: glBeginFrame...\n");
#endif
	glBeginFrame();
#ifdef _XBOX
	if (logDetailed) XBLog_Write("SDT: glBeginFrame done\n");
#endif

	if (!SP_LicenseDone)
	{
		// clear the screen if we haven't done the
		// license yet...
		glClearColor(0, 0, 0, 1);
		glClear(GL_COLOR_BUFFER_BIT);
	}

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

#ifdef _XBOX
	if (logDetailed) XBLog_Write("SDT: glBeginEXT(GL_TRIANGLE_STRIP, 4)...\n");
#endif
	glBeginEXT (GL_TRIANGLE_STRIP, 4, 0, 0, 4, 0);
		glTexCoord2f( 0,  0 );
		glVertex2f(x1, y1);
		glTexCoord2f( 1 ,  0 );
		glVertex2f(x2, y1);
		glTexCoord2f( 0, 1 );
		glVertex2f(x1, y2);
		glTexCoord2f( 1, 1 );
		glVertex2f(x2, y2);
#ifdef _XBOX
	if (logDetailed) XBLog_Write("SDT: glEnd...\n");
#endif
	glEnd();
#ifdef _XBOX
	if (logDetailed) XBLog_Write("SDT: glEndFrame...\n");
#endif
	glEndFrame();
#ifdef _XBOX
	if (logDetailed) XBLog_Write("SDT: glFlush...\n");
#endif
	glFlush();
#ifdef _XBOX
	if (logDetailed) XBLog_Write("SDT: restore states (Enable/Disable x10)...\n");
#endif

	// Restore (most) of the render states we reset
	if (alpha) glEnable(GL_ALPHA_TEST);
	else glDisable(GL_ALPHA_TEST);

	if (blend) glEnable(GL_BLEND);
	else glDisable(GL_BLEND);

	if (cull) glEnable(GL_CULL_FACE);
	else glDisable(GL_CULL_FACE);

	if (depth) glEnable(GL_DEPTH_TEST);
	else glDisable(GL_DEPTH_TEST);

	if (fog) glEnable(GL_FOG);
	else glDisable(GL_FOG);

	if (lighting) glEnable(GL_LIGHTING);
	else glDisable(GL_LIGHTING);

	if (offset) glEnable(GL_POLYGON_OFFSET_FILL);
	else glDisable(GL_POLYGON_OFFSET_FILL);

	if (scissor) glEnable(GL_SCISSOR_TEST);
	else glDisable(GL_SCISSOR_TEST);

	if (stencil) glEnable(GL_STENCIL_TEST);
	else glDisable(GL_STENCIL_TEST);

	if (texture) glEnable(GL_TEXTURE_2D);
	else glDisable(GL_TEXTURE_2D);

	// Kill the texture
#ifdef _XBOX
	if (logDetailed) XBLog_Write("SDT: glDeleteTextures...\n");
#endif
	glDeleteTextures(1, &texid);
#ifdef _XBOX
	if (logDetailed) XBLog_Write("SDT: exit\n");
#endif
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

/********
SP_DoLicense

Draws the license splash to the screen
*********/
void SP_DoLicense(void)
{
#ifdef _XBOX
	XBLog_Write("SPL: SP_DoLicense entry\n");
#endif
	if( Sys_QuickStart() )
	{
#ifdef _XBOX
		XBLog_Write("SPL: Sys_QuickStart returned true \xe2\x80\x94 early return\n");
#endif
		return;
	}
#ifdef _XBOX
	XBLog_Write("SPL: Sys_QuickStart false\n");
#endif

	// Load the license screen
	void *license;
	extern const char *Sys_RemapPath( const char *filename );
#ifdef _XBOX
	XBLog_Write("SPL: calling Sys_RemapPath...\n");
#endif
	const char *path = Sys_RemapPath( "base\\media\\LicenseScreen" );
#ifdef _XBOX
	{ char b[160]; _snprintf(b, sizeof(b), "SPL: Sys_RemapPath -> '%s'\n", path ? path : "(null)"); b[sizeof(b)-1]=0; XBLog_Write(b); }
	XBLog_Write("SPL: calling SP_LoadFileWithLanguage...\n");
#endif
	license = SP_LoadFileWithLanguage( path );
#ifdef _XBOX
	{ char b[80]; _snprintf(b, sizeof(b), "SPL: SP_LoadFileWithLanguage -> %p\n", license); b[sizeof(b)-1]=0; XBLog_Write(b); }
#endif

	if (license)
	{
#ifdef _XBOX
		XBLog_Write("SPL: calling SP_DrawTexture(license, 512, 512, 0)...\n");
		SP_SetDrawTextureContext("LicenseScreen");
#endif
		SP_DrawTexture(license, 512, 512, 0);
#ifdef _XBOX
		XBLog_Write("SPL: SP_DrawTexture returned, calling Z_Free\n");
#endif
		Z_Free(license);
	}
#ifdef _XBOX
	XBLog_Write("SPL: setting SP_LicenseDone = true\n");
#endif

	SP_LicenseDone = true;
#ifdef _XBOX
	XBLog_Write("SPL: SP_DoLicense done\n");
#endif
}

/*
SP_DrawMPLoadScreen

Draws the Multiplayer loading screen
*/
void SP_DrawMPLoadScreen( void )
{
#ifdef _XBOX
	XBLog_Write("SPL: SP_DrawMPLoadScreen entry\n");
	SP_SetDrawTextureContext("LoadMP");
#endif
	// Load the texture:
	void *image = SP_LoadFileWithLanguage("d:\\base\\media\\LoadMP");

	if( image )
	{
		SP_DrawTexture(image, 512, 512, 0);
		Z_Free(image);
	}
}

/*
SP_DrawSPLoadScreen

Draws the single player loading screen - used when skipping the logo movies
*/
void SP_DrawSPLoadScreen( void )
{
#ifdef _XBOX
	XBLog_Write("SPL: SP_DrawSPLoadScreen entry\n");
	SP_SetDrawTextureContext("LoadSP");
#endif
	// Load the texture:
	extern const char *Sys_RemapPath( const char *filename );
	void *image = SP_LoadFileWithLanguage( Sys_RemapPath("base\\media\\LoadSP") );

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
void ERR_DiscFail(bool poll)
{
#ifdef _XBOX
	{ char b[80]; _snprintf(b, sizeof(b), "ERR_DiscFail entry poll=%d\n", poll ? 1 : 0); b[sizeof(b)-1]=0; XBLog_Write(b); }
	SP_SetDrawTextureContext("DiscErr");
#endif
	// Load the texture:
	extern const char *Sys_RemapPath( const char *filename );
	void *image = SP_LoadFileWithLanguage( Sys_RemapPath("base\\media\\DiscErr") );

	if( image )
	{
		SP_DrawTexture(image, 512, 512, 0);
		Z_Free(image);
	}

	for (;;)
	{
		extern void MuteBinkSystem(void);
		MuteBinkSystem();

		extern void S_Update_(void);
		S_Update_();
	}
}
