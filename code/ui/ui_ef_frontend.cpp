// leave this at the top of all UI_xxxx files for PCH reasons.
#include "../server/exe_headers.h"

#include "ui_local.h"
#ifdef _XBOX
#include "../win32/xb_log.h"
#endif

#define EF_FRONTEND_BUTTON_COUNT 6
#define EF_FRONTEND_CONFIGURE_COUNT 3
#define EF_FRONTEND_NEWGAME_COUNT 8
#define EF_FRONTEND_FONT_COUNT 3
#define EF_FRONTEND_FONT_CHARS 256
#define EF_FRONTEND_FONT_BUFFER 20000
#define EF_FRONTEND_BUTTON_TEXT_BUFFER 14000
#define EF_FRONTEND_MBT_MAX 250
#define EF_SPLITSCREEN_BASELINE_MAP "borg1"

#define EF_FRONTEND_FONT_TINY 0
#define EF_FRONTEND_FONT_MEDIUM 1
#define EF_FRONTEND_FONT_BIG 2

// The PS2 reference capture is a 1920x899 game viewport inside the PCSX2
// window.  Keep these conversions explicit so the layout can be audited back
// to source pixels.
#define EF_PS2_VIEW_W 1920.0f
#define EF_PS2_VIEW_H 899.0f
#define EF_PS2_X(x) ((float)(x) * (640.0f / EF_PS2_VIEW_W))
#define EF_PS2_Y(y) ((float)(y) * (480.0f / EF_PS2_VIEW_H))
#define EF_PS2_W(w) EF_PS2_X(w)
#define EF_PS2_H(h) EF_PS2_Y(h)
#define EF_ARRAY_LEN(a) ((int)(sizeof(a) / sizeof((a)[0])))

#define EF_PROP_HEIGHT 16
#define EF_PROP_BIG_HEIGHT 24
#define EF_PROP_TINY_HEIGHT 10
#define EF_PROP_GAP_WIDTH 2
#define EF_PROP_GAP_TINY_WIDTH 1
#define EF_PROP_GAP_BIG_WIDTH 3
#define EF_PROP_SPACE_WIDTH 4
#define EF_PROP_SPACE_TINY_WIDTH 3
#define EF_PROP_SPACE_BIG_WIDTH 6


typedef enum {
	EF_SCREEN_MAIN,
	EF_SCREEN_NEWGAME,
	EF_SCREEN_LOADGAME,
	EF_SCREEN_CONFIGURE,
	EF_SCREEN_AUDIO,
	EF_SCREEN_VIDEO,
	EF_SCREEN_CONTROLLER,
	EF_SCREEN_STUB
} efFrontendScreen_t;

enum {
	EF_PROMPT_SELECT_MID,
	EF_PROMPT_SELECT_HIGH,
	EF_PROMPT_BACK_HIGH,
	EF_PROMPT_ACCEPT_HIGH,
	EF_PROMPT_CANCEL_HIGH,
	EF_PROMPT_LOAD_SELECT,
	EF_PROMPT_LOAD_BACK,
	EF_PROMPT_SWITCH_CORNERS,
	EF_PROMPT_DEFAULT,
	EF_PROMPT_ACCEPT,
	EF_PROMPT_CANCEL
};

enum {
	EF_UTILITY_LEFT,
	EF_UTILITY_TOP,
	EF_UTILITY_TOP_RIGHT,
	EF_UTILITY_RIGHT,
	EF_UTILITY_BOTTOM_LEFT,
	EF_UTILITY_BOTTOM_RIGHT
};

enum {
	EF_NEWGAME_TITLE,
	EF_NEWGAME_LEFT_PANEL,
	EF_NEWGAME_DIFFICULTY_HEADER,
	EF_NEWGAME_EASY,
	EF_NEWGAME_NORMAL,
	EF_NEWGAME_CHALLENGING,
	EF_NEWGAME_DIFFICULT,
	EF_NEWGAME_GENDER_HEADER,
	EF_NEWGAME_FEMALE,
	EF_NEWGAME_MALE,
	EF_NEWGAME_WARP_CORE,
	EF_NEWGAME_TUTORIAL,
	EF_NEWGAME_ENGAGE_BLOCK
};

enum {
	EF_LOADGAME_LEFT_FRAME,
	EF_LOADGAME_RIGHT_FRAME,
	EF_LOADGAME_BOTTOM_LEFT,
	EF_LOADGAME_BOTTOM_RIGHT
};

enum {
	EF_CONFIGURE_TITLE,
	EF_CONFIGURE_AUDIO,
	EF_CONFIGURE_VIDEO,
	EF_CONFIGURE_CONTROLLER
};

enum {
	EF_AUDIO_TITLE,
	EF_AUDIO_EFFECTS,
	EF_AUDIO_MUSIC,
	EF_AUDIO_VOICE,
	EF_AUDIO_SOUND
};

enum {
	EF_VIDEO_TITLE,
	EF_VIDEO_CORNER_TL,
	EF_VIDEO_CORNER_TR,
	EF_VIDEO_CORNER_BL,
	EF_VIDEO_CORNER_BR,
	EF_VIDEO_INSTRUCTIONS
};

enum {
	EF_CONTROLLER_TITLE,
	EF_CONTROLLER_FRAME_TOP_LEFT,
	EF_CONTROLLER_FRAME_TOP_RIGHT,
	EF_CONTROLLER_FRAME_LEFT_UPPER,
	EF_CONTROLLER_FRAME_LEFT_MIDDLE,
	EF_CONTROLLER_FRAME_LEFT_LOWER,
	EF_CONTROLLER_FRAME_RIGHT_UPPER,
	EF_CONTROLLER_FRAME_RIGHT_LOWER,
	EF_CONTROLLER_FRAME_BOTTOM_LEFT,
	EF_CONTROLLER_FRAME_BOTTOM_RIGHT,
	EF_CONTROLLER_LEFT_CALLOUTS,
	EF_CONTROLLER_CENTER_PAD,
	EF_CONTROLLER_ANALOG_LEFT,
	EF_CONTROLLER_ANALOG_RIGHT,
	EF_CONTROLLER_RIGHT_CALLOUTS
};

enum {
	EF_MBT_NEWGAME = 1,
	EF_MBT_LOADGAME = 2,
	EF_MBT_SETUP = 3,
	EF_MBT_EXPLOREVGER = 4,
	EF_MBT_CREDITS = 5,
	EF_MBT_QUIT = 6,
	EF_MBT_VOYAGERCREW = 7
};

typedef struct {
	float x;
	float y;
	int textEnum;
	const char *fallbackLabel;
	const char *fallbackDescription;
	const char *commandName;
	qboolean enabled;
	int color;
	int color2;
} efFrontendButton_t;

typedef struct {
	qboolean cached;
	qhandle_t whiteShader;
	qhandle_t buttonRight;
	qhandle_t buttonLeftEnd;
	qhandle_t fullButton;
	qhandle_t circle;
	qhandle_t quadrants;
	qhandle_t ps2MainTopLeftChrome;
	qhandle_t ps2UtilityBottomLeftChrome;
	qhandle_t ps2UtilityTopRightChrome;
	qhandle_t ps2ControllerTopRightChrome;
	qhandle_t ps2ControllerBottomLeftChrome;
	qhandle_t ps2LoadTopRightChrome;
	qhandle_t ps2LoadBottomLeftChrome;
	qhandle_t pauseCornerUpper;
	qhandle_t pauseCornerUpper2;
	qhandle_t bracketCorner;
	qhandle_t cornerLove;
	qhandle_t cornerLove2;
	qhandle_t lgTopLeft;
	qhandle_t lgTopRight;
	qhandle_t lgLowLeft;
	qhandle_t lgLowRight;
	qhandle_t panelCorner;
	qhandle_t monBar;
	qhandle_t monBar2;
	qhandle_t slider;
	qhandle_t leftArrow;
	qhandle_t rightArrow;
	qhandle_t xboxA;
	qhandle_t xboxB;
	qhandle_t xboxX;
	qhandle_t xboxY;
	qhandle_t xboxWhite;
	qhandle_t xboxBlack;
	qhandle_t xboxLT;
	qhandle_t xboxRT;
	qhandle_t xboxBack;
	qhandle_t xboxStart;
	qhandle_t xboxLStick;
	qhandle_t xboxRStick;
	qhandle_t xboxDpad[4];
	qhandle_t xboxController;
	qhandle_t warpCore;
	qhandle_t cursor;
} efFrontendAssets_t;


typedef struct {
	qboolean loaded;
	qhandle_t shader[EF_FRONTEND_FONT_COUNT];
	int propMap[EF_FRONTEND_FONT_COUNT][EF_FRONTEND_FONT_CHARS][3];
} efFrontendFonts_t;

static efFrontendAssets_t s_assets;
static efFrontendFonts_t s_fonts;
static qboolean s_active = qfalse;
static qboolean s_loggedDraw = qfalse;
static efFrontendScreen_t s_screen = EF_SCREEN_MAIN;
static int s_cursor = 0;
static int s_newgameDifficulty = 1;
static int s_newgameGenderMale = 1;
static qboolean s_newgameGenderTouched = qfalse;
static float s_audioEffects = 1.0f;
static float s_audioMusic = 0.25f;
static float s_audioVoice = 1.0f;
static qboolean s_audioTouched = qfalse;
static int s_videoCorner = 0;
static char s_menuSmokeTarget[32];
static int s_menuSmokeStage = 0;
static int s_menuSmokeNextRealtime = 0;
static int s_menuSmokeMainDownRemaining = 0;
static int s_menuSmokeDownRemaining = 0;
static char s_stubTitle[64];
static char s_stubLine[96];
static char s_fontBuffer[EF_FRONTEND_FONT_BUFFER];
static char s_buttonTextBuffer[EF_FRONTEND_BUTTON_TEXT_BUFFER];
static char *s_buttonText[EF_FRONTEND_MBT_MAX][2];
static qboolean s_buttonTextLoaded = qfalse;

static const char *s_configureItems[EF_FRONTEND_CONFIGURE_COUNT] = { "AUDIO", "VIDEO", "CONTROLLER" };
static const char *s_newgameItems[EF_FRONTEND_NEWGAME_COUNT] = { "EASY", "NORMAL", "CHALLENGING", "DIFFICULT", "FEMALE", "MALE", "TUTORIAL", "ENGAGE" };

#define EF_PS2_RGBA(r, g, b) { (float)(r) / 255.0f, (float)(g) / 255.0f, (float)(b) / 255.0f, 1.0f }

static vec4_t s_ps2ButtonPurple = EF_PS2_RGBA(105, 83, 145);
static vec4_t s_ps2ButtonSelected = EF_PS2_RGBA(155, 135, 189);
static vec4_t s_ps2StripPurple = EF_PS2_RGBA(121, 69, 111);
static vec4_t s_ps2TopPurple = EF_PS2_RGBA(73, 53, 83);
static vec4_t s_ps2LightBrown = EF_PS2_RGBA(171, 103, 59);
static vec4_t s_ps2DarkBrown = EF_PS2_RGBA(97, 49, 17);
static vec4_t s_ps2Gold = EF_PS2_RGBA(213, 176, 44);
static vec4_t s_ps2MapGold = EF_PS2_RGBA(193, 193, 46);
static vec4_t s_ps2SelectedText = EF_PS2_RGBA(219, 217, 223);
static vec4_t s_ps2DeepPurple = EF_PS2_RGBA(75, 15, 137);
static vec4_t s_ps2BrightPurple = EF_PS2_RGBA(163, 124, 230);
static vec4_t s_ps2MutedPurple = EF_PS2_RGBA(132, 94, 181);
static vec4_t s_ps2AudioGray = EF_PS2_RGBA(143, 143, 143);
static vec4_t s_ps2DialogGray = EF_PS2_RGBA(102, 102, 102);
static vec4_t s_ps2ControllerBody = EF_PS2_RGBA(37, 24, 48);
static vec4_t s_ps2ControllerGrip = EF_PS2_RGBA(30, 22, 38);



static efFrontendButton_t s_buttons[EF_FRONTEND_BUTTON_COUNT] = {
	{EF_PS2_X(491), EF_PS2_Y(253), EF_MBT_NEWGAME,     "NEW GAME",     "START A NEW GAME",         "ui_ef_newgame",   qtrue, CT_DKPURPLE1, CT_LTPURPLE1},
	{EF_PS2_X(491), EF_PS2_Y(356), EF_MBT_LOADGAME,    "LOAD GAME",    "LOAD A SAVED GAME",        "ui_ef_loadgame",  qtrue, CT_DKPURPLE1, CT_LTPURPLE1},
	{EF_PS2_X(491), EF_PS2_Y(459), 0,                  "HOLOMATCH",    "ENTER HOLOMATCH",          "ui_ef_holomatch",      qtrue, CT_DKPURPLE1, CT_LTPURPLE1},
	{EF_PS2_X(491), EF_PS2_Y(562), 0,                  "CONFIGURE",    "CONFIGURE OPTIONS",        "ui_ef_configure", qtrue, CT_DKPURPLE1, CT_LTPURPLE1},
	{EF_PS2_X(491), EF_PS2_Y(665), EF_MBT_VOYAGERCREW, "VOYAGER CREW", "VIEW CREW BIOGRAPHIES",    "ui_ef_crew",      qtrue, CT_DKPURPLE1, CT_LTPURPLE1},
	{EF_PS2_X(491), EF_PS2_Y(768), EF_MBT_CREDITS,     "CREDITS",      "VIEW ELITE FORCE CREDITS", "ui_ef_credits",   qtrue, CT_DKPURPLE1, CT_LTPURPLE1}
};

static void EFFe_Adjust(float *x, float *y, float *w, float *h)
{
	(void)x;
	(void)y;
	(void)w;
	(void)h;
}

static void EFFe_DrawPicSTColor(float x, float y, float w, float h, float s0, float t0, float s1, float t1, qhandle_t shader, const float *color)
{
#ifdef _XBOX
	static int s_frontendDrawPicLogBudget = 48;
#endif

	if (!shader)
	{
		return;
	}

	ui.R_SetColor(color);
	EFFe_Adjust(&x, &y, &w, &h);
#ifdef _XBOX
	if (s_frontendDrawPicLogBudget > 0)
	{
		XBLF("STEFX: EF main menu drawpic shader=%d rgba=(%g,%g,%g,%g) rect=(%g,%g %gx%g) st=(%g,%g %g,%g) drawFn=%p",
			shader, color[0], color[1], color[2], color[3], x, y, w, h, s0, t0, s1, t1, (void*)ui.R_DrawStretchPic);
		--s_frontendDrawPicLogBudget;
	}
#endif
	ui.R_DrawStretchPic(x, y, w, h, s0, t0, s1, t1, shader);
}

static void EFFe_DrawPicColor(float x, float y, float w, float h, qhandle_t shader, const float *color)
{
	float s0;
	float s1;
	float t0;
	float t1;

	if (w < 0)
	{
		w = -w;
		s0 = 1.0f;
		s1 = 0.0f;
	}
	else
	{
		s0 = 0.0f;
		s1 = 1.0f;
	}

	if (h < 0)
	{
		h = -h;
		t0 = 1.0f;
		t1 = 0.0f;
	}
	else
	{
		t0 = 0.0f;
		t1 = 1.0f;
	}

	EFFe_DrawPicSTColor(x, y, w, h, s0, t0, s1, t1, shader, color);
}

static void EFFe_DrawPic(float x, float y, float w, float h, qhandle_t shader, int colorIndex)
{
	EFFe_DrawPicColor(x, y, w, h, shader, colorTable[colorIndex]);
}

static void EFFe_ClearFontMaps(void)
{
	int f;
	int i;

	for (f = 0; f < EF_FRONTEND_FONT_COUNT; f++) {
		for (i = 0; i < EF_FRONTEND_FONT_CHARS; i++) {
			s_fonts.propMap[f][i][0] = 0;
			s_fonts.propMap[f][i][1] = 0;
			s_fonts.propMap[f][i][2] = -1;
		}
	}
}

static qboolean EFFe_ParseFontMap(const char **cursor, int fontIndex, const char *debugName)
{
	char *token;
	int ch;
	int component;

	token = COM_ParseExt(cursor, qtrue);
	if (!token[0] || Q_stricmp(token, "{")) {
#ifdef _XBOX
		XBLF("STEFX: EF frontend font parse fail map='%s' expected_open token='%s'",
			debugName, token ? token : "<null>");
#endif
		return qfalse;
	}

	for (ch = 0; ch < EF_FRONTEND_FONT_CHARS; ch++) {
		token = COM_ParseExt(cursor, qtrue);
		if (!token[0] || Q_stricmp(token, "{")) {
#ifdef _XBOX
			XBLF("STEFX: EF frontend font parse fail map='%s' char=%d expected_char_open token='%s'",
				debugName, ch, token ? token : "<null>");
#endif
			return qfalse;
		}

		for (component = 0; component < 3; component++) {
			token = COM_ParseExt(cursor, qtrue);
			if (!token[0]) {
#ifdef _XBOX
				XBLF("STEFX: EF frontend font parse fail map='%s' char=%d component=%d empty",
					debugName, ch, component);
#endif
				return qfalse;
			}
			s_fonts.propMap[fontIndex][ch][component] = atoi(token);
		}

		token = COM_ParseExt(cursor, qtrue);
		if (!token[0] || Q_stricmp(token, "}")) {
#ifdef _XBOX
			XBLF("STEFX: EF frontend font parse fail map='%s' char=%d expected_char_close token='%s'",
				debugName, ch, token ? token : "<null>");
#endif
			return qfalse;
		}
	}

	token = COM_ParseExt(cursor, qtrue);
	if (!token[0] || Q_stricmp(token, "}")) {
#ifdef _XBOX
		XBLF("STEFX: EF frontend font parse fail map='%s' expected_close token='%s'",
			debugName, token ? token : "<null>");
#endif
		return qfalse;
	}

	return qtrue;
}

static void EFFe_LoadFonts(void)
{
	fileHandle_t file;
	int len;
	const char *cursor;
	qboolean ok;

	EFFe_ClearFontMaps();
	s_fonts.shader[EF_FRONTEND_FONT_TINY] = ui.R_RegisterShaderNoMip("gfx/2d/chars_tiny.tga");
	s_fonts.shader[EF_FRONTEND_FONT_MEDIUM] = ui.R_RegisterShaderNoMip("gfx/2d/chars_medium.tga");
	s_fonts.shader[EF_FRONTEND_FONT_BIG] = ui.R_RegisterShaderNoMip("gfx/2d/chars_big.tga");

	len = ui.FS_FOpenFile("ext_data/fonts.dat", &file, FS_READ);
	if (!file) {
#ifdef _XBOX
		XBLF("STEFX: EF frontend font file missing len=%d tiny=%d med=%d big=%d",
			len, s_fonts.shader[0], s_fonts.shader[1], s_fonts.shader[2]);
#endif
		s_fonts.loaded = qfalse;
		return;
	}

	if (len <= 0 || len >= EF_FRONTEND_FONT_BUFFER) {
#ifdef _XBOX
		XBLF("STEFX: EF frontend font file bad len=%d max=%d",
			len, EF_FRONTEND_FONT_BUFFER);
#endif
		ui.FS_FCloseFile(file);
		s_fonts.loaded = qfalse;
		return;
	}

	memset(s_fontBuffer, 0, sizeof(s_fontBuffer));
	ui.FS_Read(s_fontBuffer, len, file);
	ui.FS_FCloseFile(file);

	cursor = s_fontBuffer;
	ok = EFFe_ParseFontMap(&cursor, EF_FRONTEND_FONT_TINY, "tiny");
	if (ok) {
		ok = EFFe_ParseFontMap(&cursor, EF_FRONTEND_FONT_MEDIUM, "medium");
	}
	if (ok) {
		ok = EFFe_ParseFontMap(&cursor, EF_FRONTEND_FONT_BIG, "big");
	}

	s_fonts.loaded = ok;
#ifdef _XBOX
	XBLF("STEFX: EF frontend fonts loaded=%d len=%d tiny=%d med=%d big=%d sampleA=%d/%d/%d",
		s_fonts.loaded ? 1 : 0,
		len,
		s_fonts.shader[0],
		s_fonts.shader[1],
		s_fonts.shader[2],
		s_fonts.propMap[EF_FRONTEND_FONT_MEDIUM]['A'][0],
		s_fonts.propMap[EF_FRONTEND_FONT_MEDIUM]['A'][1],
		s_fonts.propMap[EF_FRONTEND_FONT_MEDIUM]['A'][2]);
#endif
}

static void EFFe_LanguageFilename(const char *baseName, const char *baseExtension, char *finalName)
{
	char language[MAX_QPATH];
	fileHandle_t file;

	ui.Cvar_VariableStringBuffer("g_language", language, sizeof(language));
	if (language[0] == '\0' || Q_stricmp("ENGLISH", language) == 0)
	{
		Com_sprintf(finalName, MAX_QPATH, "%s.%s", baseName, baseExtension);
		return;
	}

	Com_sprintf(finalName, MAX_QPATH, "%s_%s.%s", baseName, language, baseExtension);
	ui.FS_FOpenFile(finalName, &file, FS_READ);
	if (file == 0)
	{
		Com_sprintf(finalName, MAX_QPATH, "%s.%s", baseName, baseExtension);
	}
	else
	{
		ui.FS_FCloseFile(file);
	}
}

static char *EFFe_ParseQuoted(char **cursor)
{
	char *p;
	char *start;
	char *out;

	if (!cursor || !*cursor)
	{
		return NULL;
	}

	p = *cursor;
	for (;;)
	{
		while (*p && *p <= ' ')
		{
			p++;
		}
		if (p[0] == '/' && p[1] == '/')
		{
			while (*p && *p != '\n')
			{
				p++;
			}
			continue;
		}
		if (p[0] == '/' && p[1] == '*')
		{
			p += 2;
			while (*p && !(p[0] == '*' && p[1] == '/'))
			{
				p++;
			}
			if (*p)
			{
				p += 2;
			}
			continue;
		}
		break;
	}

	while (*p && *p != '"')
	{
		p++;
	}
	if (!*p)
	{
		*cursor = NULL;
		return NULL;
	}

	p++;
	start = p;
	out = p;
	while (*p && *p != '"')
	{
		if (*p == '\\' && p[1])
		{
			p++;
		}
		*out++ = *p++;
	}
	if (*p == '"')
	{
		p++;
	}
	*out = '\0';
	*cursor = p;
	return start;
}

static int EFFe_LoadTextFile(const char *baseName, const char *extension, char *dest, int destSize)
{
	char filename[MAX_QPATH];
	char *fileBuffer;
	int len;
	int copyLen;

	EFFe_LanguageFilename(baseName, extension, filename);
	len = ui.FS_ReadFile(filename, (void **)&fileBuffer);
	if (len < 0 || !fileBuffer)
	{
#ifdef _XBOX
		XBLF("STEFX: EF frontend text missing file='%s' len=%d", filename, len);
#endif
		return -1;
	}

	copyLen = len;
	if (copyLen >= destSize)
	{
		copyLen = destSize - 1;
	}
	memcpy(dest, fileBuffer, copyLen);
	dest[copyLen] = '\0';
	ui.FS_FreeFile(fileBuffer);
#ifdef _XBOX
	XBLF("STEFX: EF frontend text loaded file='%s' len=%d copied=%d", filename, len, copyLen);
#endif
	return copyLen;
}

static void EFFe_LoadButtonText(void)
{
	char *cursor;
	char *token;
	int i;

	if (s_buttonTextLoaded)
	{
		return;
	}

	memset(s_buttonText, 0, sizeof(s_buttonText));
	s_buttonText[0][0] = "";
	s_buttonText[0][1] = NULL;

	if (EFFe_LoadTextFile("ext_data/sp_buttontext", "dat", s_buttonTextBuffer, sizeof(s_buttonTextBuffer)) >= 0)
	{
		cursor = s_buttonTextBuffer;
		i = 1;
		while (i < EF_FRONTEND_MBT_MAX)
		{
			token = EFFe_ParseQuoted(&cursor);
			if (!token)
			{
				break;
			}
			s_buttonText[i][0] = ((token[0] == '/') && (token[1] == '\0')) ? NULL : token;
			token = EFFe_ParseQuoted(&cursor);
			if (!token)
			{
				break;
			}
			s_buttonText[i][1] = ((token[0] == '/') && (token[1] == '\0')) ? NULL : token;
			i++;
		}
#ifdef _XBOX
		XBLF("STEFX: EF frontend button text parsed count=%d max=%d", i, EF_FRONTEND_MBT_MAX);
#endif
	}

	s_buttonTextLoaded = qtrue;
}

static const char *EFFe_ButtonText(const efFrontendButton_t *button, int column)
{
	if (button && button->textEnum > 0 && button->textEnum < EF_FRONTEND_MBT_MAX &&
		column >= 0 && column < 2 && s_buttonText[button->textEnum][column] && s_buttonText[button->textEnum][column][0])
	{
		return s_buttonText[button->textEnum][column];
	}

	if (column == 1)
	{
		return button && button->fallbackDescription ? button->fallbackDescription : "";
	}
	return button && button->fallbackLabel ? button->fallbackLabel : "";
}

static void EFFe_Cache(void)
{
	if (s_assets.cached)
	{
		return;
	}

	s_assets.whiteShader = ui.R_RegisterShader("white");
	s_assets.buttonRight = ui.R_RegisterShaderNoMip("menu/new/bar1.tga");
	s_assets.buttonLeftEnd = ui.R_RegisterShaderNoMip("menu/common/barbuttonleft.tga");
	s_assets.fullButton = ui.R_RegisterShaderNoMip("menu/common/full_button2.tga");
	s_assets.circle = ui.R_RegisterShaderNoMip("menu/common/circle.tga");
	s_assets.quadrants = ui.R_RegisterShaderNoMip("menu/special/quadrants.jpg");
	s_assets.ps2MainTopLeftChrome = ui.R_RegisterShaderNoMip("menu/common/ps2_main_lcars_top_left.tga");
	s_assets.ps2UtilityBottomLeftChrome = ui.R_RegisterShaderNoMip("menu/common/ps2_utility_bottom_left.tga");
	s_assets.ps2UtilityTopRightChrome = ui.R_RegisterShaderNoMip("menu/common/ps2_utility_top_right.tga");
	s_assets.ps2ControllerTopRightChrome = ui.R_RegisterShaderNoMip("menu/common/ps2_controller_top_right.tga");
	s_assets.ps2ControllerBottomLeftChrome = ui.R_RegisterShaderNoMip("menu/common/ps2_controller_bottom_left.tga");
	s_assets.ps2LoadTopRightChrome = ui.R_RegisterShaderNoMip("menu/common/ps2_load_top_right.tga");
	s_assets.ps2LoadBottomLeftChrome = ui.R_RegisterShaderNoMip("menu/common/ps2_load_bottom_left.tga");
	s_assets.pauseCornerUpper = ui.R_RegisterShaderNoMip("menu/common/corner_ll_47_7.tga");
	s_assets.pauseCornerUpper2 = ui.R_RegisterShaderNoMip("menu/common/corner_ul_47_7.tga");
	s_assets.bracketCorner = ui.R_RegisterShaderNoMip("menu/common/corner_ul_16_18.tga");
	s_assets.cornerLove = ui.R_RegisterShaderNoMip("menu/common/corner_love.tga");
	s_assets.cornerLove2 = ui.R_RegisterShaderNoMip("menu/common/corner_love_2.tga");
	s_assets.lgTopLeft = ui.R_RegisterShaderNoMip("menu/common/lg_topleft.tga");
	s_assets.lgTopRight = ui.R_RegisterShaderNoMip("menu/common/lg_topright.tga");
	s_assets.lgLowLeft = ui.R_RegisterShaderNoMip("menu/common/lg_lowleft.tga");
	s_assets.lgLowRight = ui.R_RegisterShaderNoMip("menu/common/lg_lowright.tga");
	s_assets.panelCorner = ui.R_RegisterShaderNoMip("menu/lcarscontrols/round11.tga");
	s_assets.monBar = ui.R_RegisterShaderNoMip("menu/common/mon_bar.tga");
	s_assets.monBar2 = ui.R_RegisterShaderNoMip("menu/common/monbar_2.tga");
	s_assets.slider = ui.R_RegisterShaderNoMip("menu/common/slider.tga");
	s_assets.leftArrow = ui.R_RegisterShaderNoMip("menu/common/left_arrow.tga");
	s_assets.rightArrow = ui.R_RegisterShaderNoMip("menu/common/right_arrow.tga");
	s_assets.xboxA = ui.R_RegisterShaderNoMip("menu/common/xbox_a.tga");
	s_assets.xboxB = ui.R_RegisterShaderNoMip("menu/common/xbox_b.tga");
	s_assets.xboxX = ui.R_RegisterShaderNoMip("menu/common/xbox_x.tga");
	s_assets.xboxY = ui.R_RegisterShaderNoMip("menu/common/xbox_y.tga");
	s_assets.xboxWhite = ui.R_RegisterShaderNoMip("menu/common/xbox_white.tga");
	s_assets.xboxBlack = ui.R_RegisterShaderNoMip("menu/common/xbox_black.tga");
	s_assets.xboxLT = ui.R_RegisterShaderNoMip("menu/common/xbox_lt.tga");
	s_assets.xboxRT = ui.R_RegisterShaderNoMip("menu/common/xbox_rt.tga");
	s_assets.xboxBack = ui.R_RegisterShaderNoMip("menu/common/xbox_back.tga");
	s_assets.xboxStart = ui.R_RegisterShaderNoMip("menu/common/xbox_start.tga");
	s_assets.xboxLStick = ui.R_RegisterShaderNoMip("menu/common/xbox_lstick.tga");
	s_assets.xboxRStick = ui.R_RegisterShaderNoMip("menu/common/xbox_rstick.tga");
	s_assets.xboxDpad[0] = ui.R_RegisterShaderNoMip("menu/common/xbox_dpad_up.tga");
	s_assets.xboxDpad[1] = ui.R_RegisterShaderNoMip("menu/common/xbox_dpad_down.tga");
	s_assets.xboxDpad[2] = ui.R_RegisterShaderNoMip("menu/common/xbox_dpad_left.tga");
	s_assets.xboxDpad[3] = ui.R_RegisterShaderNoMip("menu/common/xbox_dpad_right.tga");
	s_assets.xboxController = ui.R_RegisterShaderNoMip("menu/common/xbox_controller_s.tga");
	s_assets.warpCore = ui.R_RegisterShaderNoMip("menu/common/warpcore2.jpg");
	s_assets.cursor = ui.R_RegisterShaderNoMip("menu/common/cursor.tga");

	EFFe_LoadButtonText();
	EFFe_LoadFonts();

	s_assets.cached = qtrue;
#ifdef _XBOX
	XBLF("STEFX: EF main menu cache done white=%d quad=%d leftCap=%d rightBar=%d warp=%d fontSmall=%d",
		s_assets.whiteShader, s_assets.quadrants, s_assets.buttonLeftEnd, s_assets.buttonRight, s_assets.warpCore, s_fonts.shader[1]);
	ui.Printf("STEFX: EF main menu cache done white=%d quad=%d leftCap=%d rightBar=%d warp=%d fontSmall=%d\n",
		s_assets.whiteShader, s_assets.quadrants, s_assets.buttonLeftEnd, s_assets.buttonRight, s_assets.warpCore, s_fonts.shader[1]);
#endif
}

void UI_EFMainMenu_Cache(void)
{
	EFFe_Cache();
}

static float EFFe_TextWidthScaled(const char *text, int fontIndex, float scale)
{
	int gap;
	int spaceWidth;
	float width = 0.0f;
	const unsigned char *s = (const unsigned char *)text;

	if (!text || scale <= 0.0f)
	{
		return 0.0f;
	}

	gap = (fontIndex == EF_FRONTEND_FONT_TINY) ? EF_PROP_GAP_TINY_WIDTH :
		((fontIndex == EF_FRONTEND_FONT_BIG) ? EF_PROP_GAP_BIG_WIDTH : EF_PROP_GAP_WIDTH);
	spaceWidth = (fontIndex == EF_FRONTEND_FONT_TINY) ? EF_PROP_SPACE_TINY_WIDTH :
		((fontIndex == EF_FRONTEND_FONT_BIG) ? EF_PROP_SPACE_BIG_WIDTH : EF_PROP_SPACE_WIDTH);

	while (*s)
	{
		int w = (*s == ' ') ? spaceWidth : s_fonts.propMap[fontIndex][*s][2];
		if (w != -1) {
			width += (float)(w + gap) * scale;
		}
		s++;
	}

	return (width > 0.0f) ? (width - (float)gap * scale) : 0.0f;
}

static int EFFe_TextWidth(const char *text, int fontIndex)
{
	return (int)(EFFe_TextWidthScaled(text, fontIndex, 1.0f) + 0.5f);
}

static void EFFe_DrawTextScaledXYColor(float x, float y, const char *text, int fontIndex, int style, const float *color, float xScale, float yScale)
{
	const unsigned char *s;

	if (!text || !s_fonts.loaded || !s_fonts.shader[fontIndex] || xScale <= 0.0f || yScale <= 0.0f)
	{
		return;
	}

	if (style & UI_CENTER)
	{
		x -= EFFe_TextWidthScaled(text, fontIndex, xScale) * 0.5f;
	}
	else if (style & UI_RIGHT)
	{
		x -= EFFe_TextWidthScaled(text, fontIndex, xScale);
	}

	ui.R_SetColor(color);
	s = (const unsigned char *)text;
	while (*s)
	{
		int ch = *s++;
		int sx = s_fonts.propMap[fontIndex][ch][0];
		int sy = s_fonts.propMap[fontIndex][ch][1];
		int sw = s_fonts.propMap[fontIndex][ch][2];
		int gap = (fontIndex == EF_FRONTEND_FONT_TINY) ? EF_PROP_GAP_TINY_WIDTH :
			((fontIndex == EF_FRONTEND_FONT_BIG) ? EF_PROP_GAP_BIG_WIDTH : EF_PROP_GAP_WIDTH);
		int spaceWidth = (fontIndex == EF_FRONTEND_FONT_TINY) ? EF_PROP_SPACE_TINY_WIDTH :
			((fontIndex == EF_FRONTEND_FONT_BIG) ? EF_PROP_SPACE_BIG_WIDTH : EF_PROP_SPACE_WIDTH);
		float rawH, s0, t0, s1, t1, w, h, drawX, drawY;

		if (ch == ' ') {
			sw = spaceWidth;
		}
		if (sw == -1) {
			continue;
		}
		rawH = (fontIndex == EF_FRONTEND_FONT_TINY) ? (float)EF_PROP_TINY_HEIGHT :
			((fontIndex == EF_FRONTEND_FONT_BIG) ? (float)EF_PROP_BIG_HEIGHT : (float)EF_PROP_HEIGHT);
		w = (float)sw * xScale;
		h = rawH * yScale;
		s0 = (float)sx / 256.0f;
		t0 = (float)sy / 256.0f;
		s1 = (float)(sx + sw) / 256.0f;
		t1 = (float)(sy + (int)rawH) / 256.0f;
		if (ch != ' ') {
			drawX = x;
			drawY = y;
#ifdef _XBOX
			{
				static int s_frontendDrawTextLogBudget = 48;
				if (s_frontendDrawTextLogBudget > 0)
				{
					XBLF("STEFX: EF main menu drawtext ch=%d font=%d shader=%d rect=(%g,%g %gx%g) st=(%g,%g %g,%g) drawFn=%p",
						ch, fontIndex, s_fonts.shader[fontIndex], drawX, drawY, w, h, s0, t0, s1, t1, (void*)ui.R_DrawStretchPic);
					--s_frontendDrawTextLogBudget;
				}
			}
#endif
			ui.R_DrawStretchPic(drawX, drawY, w, h, s0, t0, s1, t1, s_fonts.shader[fontIndex]);
		}
		x += (float)(sw + gap) * xScale;
	}
	ui.R_SetColor(NULL);
}

static void EFFe_DrawTextScaledColor(float x, float y, const char *text, int fontIndex, int style, const float *color, float scale)
{
	EFFe_DrawTextScaledXYColor(x, y, text, fontIndex, style, color, scale, scale);
}

static void EFFe_DrawTextScaled(float x, float y, const char *text, int fontIndex, int style, int colorIndex, float scale)
{
	EFFe_DrawTextScaledColor(x, y, text, fontIndex, style, colorTable[colorIndex], scale);
}

static void EFFe_DrawText(float x, float y, const char *text, int fontIndex, int style, int colorIndex)
{
	EFFe_DrawTextScaled(x, y, text, fontIndex, style, colorIndex, 1.0f);
}

static void EFFe_DrawPs2PicColor(float x, float y, float w, float h, qhandle_t shader, const float *color);
static void EFFe_DrawPs2PicFlipXColor(float x, float y, float w, float h, qhandle_t shader, const float *color);
static void EFFe_DrawPs2TextColor(float x, float y, const char *text, int fontIndex, int style, const float *color, float xScale, float yScale);

static float EFFe_FontPs2Height(int fontIndex, float yScale)
{
	float rawH;

	rawH = (fontIndex == EF_FRONTEND_FONT_TINY) ? (float)EF_PROP_TINY_HEIGHT :
		((fontIndex == EF_FRONTEND_FONT_BIG) ? (float)EF_PROP_BIG_HEIGHT : (float)EF_PROP_HEIGHT);
	return rawH * yScale * (EF_PS2_VIEW_H / 480.0f);
}

static float EFFe_CenteredPs2TextY(float y, float h, int fontIndex, float yScale)
{
	return y + (h - EFFe_FontPs2Height(fontIndex, yScale)) * 0.5f;
}

static void EFFe_DrawButton(const efFrontendButton_t *button, int index)
{
	const float *fillColor = (index == s_cursor) ? s_ps2ButtonSelected : s_ps2ButtonPurple;
	const float *textColor = (index == s_cursor) ? s_ps2SelectedText : colorTable[CT_BLACK];
	float buttonH = EF_PS2_H(86);
	float buttonY = button->y / (480.0f / EF_PS2_VIEW_H);

	if (index >= 0 && index < EF_FRONTEND_BUTTON_COUNT)
	{
		EFFe_DrawPs2PicColor(401.0f, buttonY, 112.0f, 86.0f, s_assets.buttonLeftEnd, fillColor);
		EFFe_DrawPicColor(button->x, button->y, EF_PS2_W(394), buttonH, s_assets.whiteShader, fillColor);
		EFFe_DrawPs2TextColor(515.0f, EFFe_CenteredPs2TextY(buttonY, 86.0f, EF_FRONTEND_FONT_BIG, 1.0f),
			EFFe_ButtonText(button, 0),
			EF_FRONTEND_FONT_BIG,
			UI_LEFT,
			textColor,
			0.82f,
			1.00f);
	}
}

static void EFFe_DrawQuadrantLabel(float x, float y, const char *text)
{
	EFFe_DrawTextScaledXYColor(x, y, text, EF_FRONTEND_FONT_MEDIUM, UI_LEFT, s_ps2MapGold, 0.835f, 0.78f);
}

static void EFFe_DrawPs2Pic(float x, float y, float w, float h, qhandle_t shader, int colorIndex)
{
	EFFe_DrawPic(EF_PS2_X(x), EF_PS2_Y(y), EF_PS2_W(w), EF_PS2_H(h), shader, colorIndex);
}

static void EFFe_DrawPs2PicColor(float x, float y, float w, float h, qhandle_t shader, const float *color)
{
	EFFe_DrawPicColor(EF_PS2_X(x), EF_PS2_Y(y), EF_PS2_W(w), EF_PS2_H(h), shader, color);
}

static void EFFe_DrawPs2PicSTColor(float x, float y, float w, float h, float s0, float t0, float s1, float t1, qhandle_t shader, const float *color)
{
	EFFe_DrawPicSTColor(EF_PS2_X(x), EF_PS2_Y(y), EF_PS2_W(w), EF_PS2_H(h), s0, t0, s1, t1, shader, color);
}

static void EFFe_DrawPs2PicFlipX(float x, float y, float w, float h, qhandle_t shader, int colorIndex)
{
	EFFe_DrawPic(EF_PS2_X(x), EF_PS2_Y(y), -EF_PS2_W(w), EF_PS2_H(h), shader, colorIndex);
}

static void EFFe_DrawPs2PicFlipXColor(float x, float y, float w, float h, qhandle_t shader, const float *color)
{
	EFFe_DrawPicColor(EF_PS2_X(x), EF_PS2_Y(y), -EF_PS2_W(w), EF_PS2_H(h), shader, color);
}

static void EFFe_DrawPs2PicFlipY(float x, float y, float w, float h, qhandle_t shader, int colorIndex)
{
	EFFe_DrawPic(EF_PS2_X(x), EF_PS2_Y(y), EF_PS2_W(w), -EF_PS2_H(h), shader, colorIndex);
}

static void EFFe_DrawPs2PicFlipYColor(float x, float y, float w, float h, qhandle_t shader, const float *color)
{
	EFFe_DrawPicColor(EF_PS2_X(x), EF_PS2_Y(y), EF_PS2_W(w), -EF_PS2_H(h), shader, color);
}

static void EFFe_DrawPs2PicFlipXY(float x, float y, float w, float h, qhandle_t shader, int colorIndex)
{
	EFFe_DrawPic(EF_PS2_X(x), EF_PS2_Y(y), -EF_PS2_W(w), -EF_PS2_H(h), shader, colorIndex);
}

static void EFFe_DrawPs2PicFlipXYColor(float x, float y, float w, float h, qhandle_t shader, const float *color)
{
	EFFe_DrawPicColor(EF_PS2_X(x), EF_PS2_Y(y), -EF_PS2_W(w), -EF_PS2_H(h), shader, color);
}

static void EFFe_DrawPs2Rect(float x, float y, float w, float h, int colorIndex)
{
	EFFe_DrawPs2Pic(x, y, w, h, s_assets.whiteShader, colorIndex);
}

static void EFFe_DrawPs2RectColor(float x, float y, float w, float h, const float *color)
{
	EFFe_DrawPs2PicColor(x, y, w, h, s_assets.whiteShader, color);
}

static void EFFe_DrawPs2PanelBracket(float x, float y, float w, qboolean rightSide, qboolean lowerHalf)
{
	const float bracketH = 240.0f;
	const float capH = 60.0f;
	const float stemStart = 31.0f;
	const float stemW = 24.0f;
	const float upperStemExtra = 2.0f;
	float stemX = rightSide ? (x + w - stemW) : x;

	if (lowerHalf)
	{
		if (rightSide)
		{
			EFFe_DrawPs2PicFlipXYColor(x, y + bracketH - capH, w, capH, s_assets.panelCorner, s_ps2StripPurple);
		}
		else
		{
			EFFe_DrawPs2PicFlipYColor(x, y + bracketH - capH, w, capH, s_assets.panelCorner, s_ps2StripPurple);
		}
		EFFe_DrawPs2RectColor(stemX, y, stemW, bracketH - stemStart, s_ps2StripPurple);
	}
	else
	{
		if (rightSide)
		{
			EFFe_DrawPs2PicFlipXColor(x, y, w, capH, s_assets.panelCorner, s_ps2StripPurple);
		}
		else
		{
			EFFe_DrawPs2PicColor(x, y, w, capH, s_assets.panelCorner, s_ps2StripPurple);
		}
		EFFe_DrawPs2RectColor(stemX, y + stemStart, stemW, bracketH - stemStart + upperStemExtra, s_ps2StripPurple);
	}
}

static const char *EFFe_ScreenName(efFrontendScreen_t screen)
{
	switch (screen)
	{
	case EF_SCREEN_MAIN:
		return "main";
	case EF_SCREEN_NEWGAME:
		return "newgame";
	case EF_SCREEN_LOADGAME:
		return "loadgame";
	case EF_SCREEN_CONFIGURE:
		return "configure";
	case EF_SCREEN_AUDIO:
		return "audio";
	case EF_SCREEN_VIDEO:
		return "video";
	case EF_SCREEN_CONTROLLER:
		return "controller";
	case EF_SCREEN_STUB:
		return "stub";
	default:
		return "unknown";
	}
}

static qboolean EFFe_IsAcceptKey(int key)
{
	return key == A_ENTER || key == A_KP_ENTER || key == A_MOUSE1 || key == A_JOY15;
}

static qboolean EFFe_IsBackKey(int key)
{
	return key == A_ESCAPE || key == A_MOUSE2 || key == A_JOY13 || key == A_JOY14 || key == A_BACKSPACE;
}

static qboolean EFFe_IsUpKey(int key)
{
	return key == A_CURSOR_UP || key == A_JOY5;
}

static qboolean EFFe_IsDownKey(int key)
{
	return key == A_CURSOR_DOWN || key == A_JOY7;
}

static qboolean EFFe_IsLeftKey(int key)
{
	return key == A_CURSOR_LEFT || key == A_JOY8;
}

static qboolean EFFe_IsRightKey(int key)
{
	return key == A_CURSOR_RIGHT || key == A_JOY6;
}

static float EFFe_Clamp01(float value)
{
	if (value < 0.0f)
	{
		return 0.0f;
	}
	if (value > 1.0f)
	{
		return 1.0f;
	}
	return value;
}

static void EFFe_DrawPromptLine(float x, float y, qhandle_t icon, const char *label)
{
	if (icon)
	{
		EFFe_DrawPic(x, y, 18.0f, 18.0f, icon, CT_WHITE);
	}
	EFFe_DrawTextScaledXYColor(x + 27.0f, y - 1.0f, ":", EF_FRONTEND_FONT_BIG, UI_LEFT, colorTable[CT_WHITE], 0.55f, 0.78f);
	EFFe_DrawTextScaledXYColor(x + 43.0f, y - 1.0f, label, EF_FRONTEND_FONT_BIG, UI_LEFT, colorTable[CT_WHITE], 0.55f, 0.78f);
}

static void EFFe_DrawPs2PromptIcon(float x, float y, qhandle_t icon)
{
	if (icon)
	{
		EFFe_DrawPs2Pic(x, y, 60.0f, 58.0f, icon, CT_WHITE);
	}
}

static void EFFe_DrawPromptLabel(float x, float y, const char *label)
{
	EFFe_DrawPs2TextColor(x, y, ":", EF_FRONTEND_FONT_BIG, UI_LEFT, colorTable[CT_WHITE], 0.68f, 1.02f);
	EFFe_DrawPs2TextColor(x + 44.0f, y, label, EF_FRONTEND_FONT_BIG, UI_LEFT, colorTable[CT_WHITE], 0.68f, 1.02f);
}

static void EFFe_DrawPanelCode(float x, float y, const char *code)
{
	EFFe_DrawPs2TextColor(x, y, code, EF_FRONTEND_FONT_TINY, UI_RIGHT, colorTable[CT_BLACK], 0.82f, 1.04f);
}

static void EFFe_DrawPromptTopSelectOnly(void)
{
	EFFe_DrawPs2PromptIcon(1385.0f, 68.0f, s_assets.xboxA);
	EFFe_DrawPromptLabel(1460.0f, 80.0f, "Select");
}

static void EFFe_DrawPromptTopSelectBack(void)
{
	EFFe_DrawPs2PromptIcon(1385.0f, 40.0f, s_assets.xboxA);
	EFFe_DrawPromptLabel(1460.0f, 52.0f, "Select");
	EFFe_DrawPs2PromptIcon(1385.0f, 96.0f, s_assets.xboxY);
	EFFe_DrawPromptLabel(1460.0f, 108.0f, "Back");
}

static void EFFe_DrawPromptTopAcceptCancel(void)
{
	EFFe_DrawPs2PromptIcon(1385.0f, 40.0f, s_assets.xboxA);
	EFFe_DrawPromptLabel(1460.0f, 52.0f, "Accept");
	EFFe_DrawPs2PromptIcon(1385.0f, 96.0f, s_assets.xboxY);
	EFFe_DrawPromptLabel(1460.0f, 108.0f, "Cancel");
}

static void EFFe_DrawControllerAcceptCancelPrompt(void)
{
	EFFe_DrawPs2PromptIcon(1215.0f, 58.0f, s_assets.xboxA);
	EFFe_DrawPromptLabel(1288.0f, 70.0f, "Accept");
	EFFe_DrawPs2PromptIcon(1510.0f, 58.0f, s_assets.xboxY);
	EFFe_DrawPromptLabel(1583.0f, 70.0f, "Cancel");
}

static void EFFe_DrawLoadPrompt(void)
{
	EFFe_DrawPs2PromptIcon(690.0f, 586.0f, s_assets.xboxA);
	EFFe_DrawPromptLabel(765.0f, 598.0f, "Select");
	EFFe_DrawPs2PromptIcon(995.0f, 586.0f, s_assets.xboxY);
	EFFe_DrawPromptLabel(1070.0f, 598.0f, "Back");
}

static void EFFe_DrawVideoPrompt(void)
{
	EFFe_DrawPs2PromptIcon(775.0f, 610.0f, s_assets.xboxX);
	EFFe_DrawPromptLabel(850.0f, 622.0f, "Switch Corners");
	EFFe_DrawPs2PromptIcon(570.0f, 690.0f, s_assets.xboxB);
	EFFe_DrawPromptLabel(645.0f, 702.0f, "Default");
	EFFe_DrawPs2PromptIcon(895.0f, 690.0f, s_assets.xboxA);
	EFFe_DrawPromptLabel(975.0f, 702.0f, "Accept");
	EFFe_DrawPs2PromptIcon(1215.0f, 690.0f, s_assets.xboxY);
	EFFe_DrawPromptLabel(1300.0f, 702.0f, "Cancel");
}

static void EFFe_DrawPs2TextColor(float x, float y, const char *text, int fontIndex, int style, const float *color, float xScale, float yScale)
{
	EFFe_DrawTextScaledXYColor(EF_PS2_X(x), EF_PS2_Y(y), text, fontIndex, style, color, xScale, yScale);
}

static void EFFe_DrawTitleText(float x, float y, const char *title)
{
	EFFe_DrawPs2TextColor(x, y, title, EF_FRONTEND_FONT_BIG, UI_LEFT, s_ps2Gold, 1.06f, 1.20f);
}

static void EFFe_DrawMenuText(float x, float y, const char *text, const float *color)
{
	EFFe_DrawPs2TextColor(x, y, text, EF_FRONTEND_FONT_BIG, UI_LEFT, color, 0.82f, 1.00f);
}

static void EFFe_DrawSmallMenuText(float x, float y, const char *text, const float *color)
{
	EFFe_DrawPs2TextColor(x, y, text, EF_FRONTEND_FONT_MEDIUM, UI_LEFT, color, 0.92f, 1.03f);
}

static void EFFe_DrawHeaderText(float x, float y, const char *text)
{
	EFFe_DrawPs2TextColor(x, y, text, EF_FRONTEND_FONT_BIG, UI_LEFT, s_ps2SelectedText, 0.82f, 1.00f);
}

static void EFFe_DrawMainTopLeftChrome(void)
{
	EFFe_DrawPs2Pic(181.0f, 23.0f, 449.0f, 318.0f, s_assets.ps2MainTopLeftChrome, CT_WHITE);
	EFFe_DrawPs2RectColor(373.0f, 154.0f, 256.0f, 30.0f, s_ps2TopPurple);
}

static void EFFe_DrawMainTopChrome(const char *title, qboolean backPrompt)
{
	EFFe_DrawPic(0.0f, 0.0f, 640.0f, 480.0f, s_assets.whiteShader, CT_BLACK);

	EFFe_DrawMainTopLeftChrome();
	EFFe_DrawPs2RectColor(640.0f, 154.0f, 35.0f, 30.0f, s_ps2StripPurple);
	EFFe_DrawPs2RectColor(685.0f, 169.0f, 255.0f, 15.0f, s_ps2StripPurple);
	EFFe_DrawPs2RectColor(948.0f, 154.0f, 520.0f, 30.0f, s_ps2LightBrown);
	EFFe_DrawPs2RectColor(1475.0f, 154.0f, 249.0f, 30.0f, s_ps2LightBrown);
	EFFe_DrawPs2RectColor(641.0f, 195.0f, 34.0f, 32.0f, s_ps2DarkBrown);
	EFFe_DrawPs2RectColor(685.0f, 195.0f, 255.0f, 15.0f, s_ps2LightBrown);
	EFFe_DrawPs2RectColor(948.0f, 196.0f, 520.0f, 31.0f, s_ps2DarkBrown);
	EFFe_DrawPs2RectColor(1475.0f, 196.0f, 249.0f, 31.0f, s_ps2LightBrown);
	EFFe_DrawPicColor(EF_PS2_X(181), EF_PS2_Y(341), EF_PS2_W(192), EF_PS2_H(393), s_assets.whiteShader, s_ps2LightBrown);
	EFFe_DrawPicColor(EF_PS2_X(181), EF_PS2_Y(742), EF_PS2_W(192), EF_PS2_H(138), s_assets.whiteShader, s_ps2DarkBrown);
	EFFe_DrawTitleText(435.0f, 66.0f, title);
	if (backPrompt)
	{
		EFFe_DrawPromptTopSelectBack();
	}
	else
	{
		EFFe_DrawPromptTopSelectOnly();
	}
}

static void EFFe_DrawLoadFrame(void)
{
	EFFe_DrawPs2RectColor(199.0f, 49.0f, 1238.0f, 34.0f, s_ps2MutedPurple);
	EFFe_DrawPs2RectColor(199.0f, 49.0f, 77.0f, 262.0f, s_ps2MutedPurple);
	EFFe_DrawPs2RectColor(199.0f, 317.0f, 77.0f, 376.0f, s_ps2MutedPurple);
	EFFe_DrawPs2RectColor(199.0f, 699.0f, 77.0f, 107.0f, s_ps2MutedPurple);
	EFFe_DrawPs2PicColor(198.0f, 806.0f, 120.0f, 90.0f, s_assets.ps2LoadBottomLeftChrome, s_ps2MutedPurple);
	EFFe_DrawPs2RectColor(1445.0f, 49.0f, 175.0f, 34.0f, s_ps2MutedPurple);
	EFFe_DrawPs2PicColor(1620.0f, 49.0f, 101.0f, 142.0f, s_assets.ps2LoadTopRightChrome, s_ps2MutedPurple);
	EFFe_DrawPs2RectColor(1644.0f, 192.0f, 77.0f, 63.0f, s_ps2MutedPurple);
	EFFe_DrawPs2RectColor(1644.0f, 261.0f, 77.0f, 576.0f, s_ps2MutedPurple);
	EFFe_DrawPs2RectColor(318.0f, 837.0f, 792.0f, 34.0f, s_ps2MutedPurple);
	EFFe_DrawPs2RectColor(1117.0f, 837.0f, 527.0f, 34.0f, s_ps2MutedPurple);
	EFFe_DrawPs2RectColor(1644.0f, 837.0f, 77.0f, 34.0f, s_ps2MutedPurple);
}

static void EFFe_DrawUtilityCodes(const char *title)
{
	const char *codeTop;
	const char *codeMid;
	const char *codeStrip;
	const char *codeBottom;

	codeTop = "5-0987";
	codeMid = "16116";
	codeStrip = "28430";
	codeBottom = "1701-8";
	if (title && !Q_stricmp(title, "ELITE FORCE : AUDIO"))
	{
		codeTop = "1176";
		codeMid = "9214";
		codeStrip = "2510-81";
		codeBottom = "1001001";
	}
	else if (title && !Q_stricmp(title, "ELITE FORCE : ADJUST SCREEN SIZE"))
	{
		codeTop = "207";
		codeMid = "44909";
		codeStrip = "357";
		codeBottom = "456730-1";
	}

	EFFe_DrawPanelCode(452.0f, 62.0f, codeTop);
	EFFe_DrawPanelCode(452.0f, 325.0f, codeMid);
	EFFe_DrawPanelCode(452.0f, 382.0f, codeStrip);
	EFFe_DrawPanelCode(452.0f, 778.0f, codeBottom);
}

static void EFFe_DrawUtilityChrome(const char *title, qboolean acceptCancel, qboolean drawTopPrompts)
{
	const float *leftTallColor = acceptCancel ? s_ps2StripPurple : s_ps2LightBrown;

	EFFe_DrawPic(0.0f, 0.0f, 640.0f, 480.0f, s_assets.whiteShader, CT_BLACK);

	EFFe_DrawPs2RectColor(236.0f, 47.0f, 235.0f, 222.0f, s_ps2DarkBrown);
	EFFe_DrawPs2RectColor(236.0f, 276.0f, 235.0f, 99.0f, s_ps2DarkBrown);
	EFFe_DrawPs2RectColor(236.0f, 379.0f, 235.0f, 357.0f, leftTallColor);
	EFFe_DrawPs2Pic(235.0f, 742.0f, 318.0f, 119.0f, s_assets.ps2UtilityBottomLeftChrome, CT_WHITE);
	EFFe_DrawPs2RectColor(236.0f, 742.0f, 228.0f, 50.0f, s_ps2DarkBrown);
	EFFe_DrawPs2RectColor(326.0f, 792.0f, 223.0f, 66.0f, s_ps2DarkBrown);
	EFFe_DrawPs2RectColor(496.0f, 189.0f, 1051.0f, 31.0f, s_ps2MutedPurple);
	EFFe_DrawPs2RectColor(1552.0f, 189.0f, 110.0f, 31.0f, s_ps2MutedPurple);
	EFFe_DrawPs2Pic(1551.0f, 189.0f, 144.0f, 42.0f, s_assets.ps2UtilityTopRightChrome, CT_WHITE);
	EFFe_DrawPs2RectColor(1552.0f, 189.0f, 110.0f, 31.0f, s_ps2MutedPurple);
	EFFe_DrawPs2RectColor(1566.0f, 234.0f, 128.0f, 540.0f, s_ps2MutedPurple);
	EFFe_DrawPs2RectColor(558.0f, 792.0f, 794.0f, 66.0f, s_ps2LightBrown);
	EFFe_DrawPs2RectColor(1359.0f, 792.0f, 335.0f, 66.0f, s_ps2LightBrown);

	EFFe_DrawUtilityCodes(title);

	if (title && !Q_stricmp(title, "ELITE FORCE : ADJUST SCREEN SIZE"))
	{
		EFFe_DrawPs2TextColor(531.0f, 89.0f, title, EF_FRONTEND_FONT_BIG, UI_LEFT, s_ps2Gold, 1.05f, 1.20f);
	}
	else
	{
		EFFe_DrawTitleText(531.0f, 89.0f, title);
	}
	if (!drawTopPrompts)
	{
		return;
	}
	if (acceptCancel)
	{
		EFFe_DrawPromptTopAcceptCancel();
	}
	else
	{
		EFFe_DrawPromptTopSelectBack();
	}
}

static void EFFe_DrawMainChildChrome(const char *title, qboolean backPrompt)
{
	EFFe_DrawMainTopChrome(title, backPrompt);
}

static void EFFe_DrawRoundButton(float capX, float x, float y, float w, float h, const char *label, const float *fill, const float *textColor)
{
	float capW = x - capX;
	if (capW < 126.0f)
	{
		capW = 126.0f;
	}
	EFFe_DrawPs2PicColor(capX, y, capW, h, s_assets.buttonLeftEnd, fill);
	EFFe_DrawPs2PicColor(x, y, w, h, s_assets.buttonRight, fill);
	if (label && label[0])
	{
		EFFe_DrawMenuText(x + 25.0f, EFFe_CenteredPs2TextY(y, h, EF_FRONTEND_FONT_BIG, 1.0f), label, textColor);
	}
}

static void EFFe_DrawRectButton(float x, float y, float w, float h, const char *label, const float *fill, const float *textColor)
{
	EFFe_DrawPs2RectColor(x, y, w, h, fill);
	if (label && label[0])
	{
		EFFe_DrawMenuText(x + 32.0f, EFFe_CenteredPs2TextY(y, h, EF_FRONTEND_FONT_BIG, 1.0f), label, textColor);
	}
}

static void EFFe_DrawRightRoundButton(float x, float y, float w, float h, const char *label, const float *fill, const float *textColor)
{
	EFFe_DrawPs2RectColor(x, y, w - 54.0f, h, fill);
	EFFe_DrawPs2PicColor(x, y, w, h, s_assets.buttonRight, fill);
	if (label && label[0])
	{
		EFFe_DrawMenuText(x + 34.0f, EFFe_CenteredPs2TextY(y, h, EF_FRONTEND_FONT_BIG, 1.0f), label, textColor);
	}
}

static void EFFe_DrawNewGameScreen(void)
{
	int i;
	const char *difficulty[4] = { "EASY", "NORMAL", "CHALLENGING", "DIFFICULT" };
	const char *gender[2] = { "FEMALE", "MALE" };

	EFFe_DrawMainChildChrome("ELITE FORCE : NEW GAME", qtrue);
	EFFe_DrawPs2RectColor(181.0f, 341.0f, 192.0f, 393.0f, s_ps2StripPurple);
	EFFe_DrawPs2RectColor(181.0f, 742.0f, 192.0f, 138.0f, s_ps2DarkBrown);
	EFFe_DrawPanelCode(358.0f, 391.0f, "45");
	EFFe_DrawPanelCode(358.0f, 453.0f, "7688200");
	EFFe_DrawPanelCode(358.0f, 798.0f, "9955");

	EFFe_DrawRectButton(415.0f, 238.0f, 513.0f, 83.0f, "GAME DIFFICULTY", s_ps2DeepPurple, s_ps2SelectedText);
	for (i = 0; i < 4; i++)
	{
		qboolean selected = (s_cursor == i);
		qboolean active = (s_newgameDifficulty == i);
		EFFe_DrawRightRoundButton(415.0f, 332.0f + (float)i * 74.0f, 513.0f, 66.0f,
			difficulty[i],
			selected ? s_ps2ButtonSelected : (active ? s_ps2BrightPurple : s_ps2ButtonPurple),
			selected || active ? s_ps2SelectedText : colorTable[CT_BLACK]);
	}

	EFFe_DrawRectButton(415.0f, 631.0f, 513.0f, 85.0f, "GENDER", s_ps2DeepPurple, s_ps2SelectedText);
	for (i = 0; i < 2; i++)
	{
		qboolean selected = (s_cursor == i + 4);
		qboolean active = (s_newgameGenderMale ? i == 1 : i == 0);
		EFFe_DrawRightRoundButton(415.0f, 725.0f + (float)i * 75.0f, 513.0f, 66.0f,
			gender[i],
			selected || active ? s_ps2BrightPurple : s_ps2ButtonPurple,
			selected || active ? s_ps2SelectedText : colorTable[CT_BLACK]);
	}

	EFFe_DrawPs2PicSTColor(954.0f, 237.0f, 168.0f, 639.0f, 0.0f, 0.0f, 0.5f, 1.0f, s_assets.warpCore, colorTable[CT_WHITE]);
	EFFe_DrawRoundButton(1197.0f, 1292.0f, 313.0f, 425.0f, 94.0f, "TUTORIAL",
		s_cursor == 6 ? s_ps2ButtonSelected : s_ps2ButtonPurple,
		colorTable[CT_BLACK]);
	EFFe_DrawPs2PicColor(1115.0f, 552.0f, 606.0f, 66.0f, s_assets.buttonRight, s_ps2StripPurple);
	EFFe_DrawPs2RectColor(1115.0f, 585.0f, 606.0f, 22.0f, s_ps2StripPurple);
	EFFe_DrawPs2RectColor(1115.0f, 607.0f, 606.0f, 12.0f, colorTable[CT_BLACK]);
	EFFe_DrawPs2RectColor(1292.0f, 618.0f, 429.0f, 205.0f,
		s_cursor == 7 ? s_ps2ButtonSelected : s_ps2ButtonPurple);
	EFFe_DrawPs2TextColor(1327.0f, 737.0f, "ENGAGE", EF_FRONTEND_FONT_BIG, UI_LEFT, colorTable[CT_BLACK], 0.82f, 1.00f);
}

static void EFFe_DrawLoadGameScreen(void)
{
	EFFe_DrawPic(0.0f, 0.0f, 640.0f, 480.0f, s_assets.whiteShader, CT_BLACK);
	EFFe_DrawLoadFrame();

	EFFe_DrawPs2RectColor(535.0f, 206.0f, 850.0f, 487.0f, s_ps2DialogGray);
	EFFe_DrawPs2RectColor(546.0f, 216.0f, 828.0f, 467.0f, colorTable[CT_BLACK]);
	EFFe_DrawPs2TextColor(960.0f, 300.0f, "An Xbox storage device,", EF_FRONTEND_FONT_MEDIUM, UI_CENTER, colorTable[CT_WHITE], 1.28f, 1.52f);
	EFFe_DrawPs2TextColor(960.0f, 356.0f, "with saved STV: Elite Force", EF_FRONTEND_FONT_MEDIUM, UI_CENTER, colorTable[CT_WHITE], 1.28f, 1.52f);
	EFFe_DrawPs2TextColor(960.0f, 412.0f, "games, was not detected.", EF_FRONTEND_FONT_MEDIUM, UI_CENTER, colorTable[CT_WHITE], 1.28f, 1.52f);
	EFFe_DrawPs2TextColor(960.0f, 516.0f, "OK", EF_FRONTEND_FONT_BIG, UI_CENTER, s_ps2MapGold, 0.74f, 1.00f);
	EFFe_DrawLoadPrompt();
}

static void EFFe_DrawStubScreen(void)
{
	const char *title = s_stubTitle[0] ? s_stubTitle : "ELITE FORCE";
	const char *line = s_stubLine[0] ? s_stubLine : "THIS MENU IS NOT AVAILABLE YET";

	EFFe_DrawMainChildChrome(title, qtrue);
	EFFe_DrawPs2RectColor(535.0f, 360.0f, 850.0f, 260.0f, s_ps2DialogGray);
	EFFe_DrawPs2RectColor(546.0f, 371.0f, 828.0f, 238.0f, colorTable[CT_BLACK]);
	EFFe_DrawPs2TextColor(960.0f, 455.0f, "COMING SOON", EF_FRONTEND_FONT_BIG, UI_CENTER, s_ps2MapGold, 0.82f, 1.00f);
	EFFe_DrawPs2TextColor(960.0f, 520.0f, line, EF_FRONTEND_FONT_MEDIUM, UI_CENTER, colorTable[CT_WHITE], 0.94f, 1.08f);
}

static void EFFe_DrawConfigureScreen(void)
{
	EFFe_DrawUtilityChrome("ELITE FORCE : CONFIGURE", qfalse, qtrue);
	EFFe_DrawRoundButton(723.0f, 829.0f, 294.0f, 513.0f, 104.0f, "AUDIO",
		s_cursor == 0 ? s_ps2ButtonSelected : s_ps2ButtonPurple,
		s_cursor == 0 ? s_ps2SelectedText : colorTable[CT_BLACK]);
	EFFe_DrawRoundButton(723.0f, 829.0f, 425.0f, 513.0f, 104.0f, "VIDEO",
		s_cursor == 1 ? s_ps2ButtonSelected : s_ps2ButtonPurple,
		s_cursor == 1 ? s_ps2SelectedText : colorTable[CT_BLACK]);
	EFFe_DrawRoundButton(723.0f, 829.0f, 556.0f, 513.0f, 104.0f, "CONTROLLER",
		s_cursor == 2 ? s_ps2ButtonSelected : s_ps2ButtonPurple,
		s_cursor == 2 ? s_ps2SelectedText : colorTable[CT_BLACK]);
}

static void EFFe_DrawSlider(float x, float y, float w, float h, float value, qboolean selected)
{
	int i;
	float fillW;
	float knobX;

	EFFe_DrawPs2RectColor(x, y, w, h, s_ps2MutedPurple);
	EFFe_DrawPs2RectColor(x + 4.0f, y + 4.0f, w - 8.0f, h - 8.0f, colorTable[CT_BLACK]);
	fillW = (w - 10.0f) * EFFe_Clamp01(value);
	EFFe_DrawPs2RectColor(x + 5.0f, y + 5.0f, fillW, h - 10.0f, selected ? colorTable[CT_WHITE] : s_ps2AudioGray);
	EFFe_DrawPs2PicColor(x + 5.0f, y + 5.0f, w - 10.0f, h - 10.0f, s_assets.monBar2, s_ps2MutedPurple);
	for (i = 1; i < 4; i++)
	{
		EFFe_DrawPs2RectColor(x + (w * (float)i / 4.0f), y + 5.0f, 4.0f, h - 10.0f, s_ps2MutedPurple);
	}
	knobX = x + 5.0f + fillW - 22.0f;
	if (knobX < x + 8.0f)
	{
		knobX = x + 8.0f;
	}
	if (knobX > x + w - 42.0f)
	{
		knobX = x + w - 42.0f;
	}
	EFFe_DrawPs2PicColor(knobX, y - 4.0f, 48.0f, h + 8.0f, s_assets.slider, selected ? s_ps2BrightPurple : s_ps2DeepPurple);
}

static void EFFe_DrawAudioRow(float y, const char *label, float value, int index)
{
	qboolean selected;
	selected = (s_cursor == index);
	EFFe_DrawPs2RectColor(553.0f, y, 475.0f, 76.0f, selected ? s_ps2ButtonSelected : s_ps2ButtonPurple);
	EFFe_DrawMenuText(578.0f, EFFe_CenteredPs2TextY(y, 76.0f, EF_FRONTEND_FONT_BIG, 1.0f),
		label, selected ? s_ps2SelectedText : colorTable[CT_BLACK]);
	if (index < 3)
	{
		EFFe_DrawSlider(1065.0f, y, 425.0f, 76.0f, value, selected);
	}
}

static void EFFe_DrawAudioScreen(void)
{
	EFFe_DrawUtilityChrome("ELITE FORCE : AUDIO", qtrue, qtrue);
	EFFe_DrawAudioRow(281.0f, "EFFECTS VOLUME", s_audioEffects, 0);
	EFFe_DrawAudioRow(378.0f, "MUSIC VOLUME", s_audioMusic, 1);
	EFFe_DrawAudioRow(476.0f, "VOICE VOLUME", s_audioVoice, 2);
	EFFe_DrawAudioRow(573.0f, "SOUND QUALITY", 0.0f, 3);
	EFFe_DrawPs2TextColor(1082.0f,
		EFFe_CenteredPs2TextY(573.0f, 76.0f, EF_FRONTEND_FONT_MEDIUM, 1.20f),
		"Stereo", EF_FRONTEND_FONT_MEDIUM, UI_LEFT, colorTable[CT_WHITE], 1.08f, 1.20f);
}

static void EFFe_DrawVideoScreen(void)
{
	EFFe_DrawUtilityChrome("ELITE FORCE : ADJUST SCREEN SIZE", qtrue, qfalse);
	EFFe_DrawPs2RectColor(173.0f, 9.0f, 300.0f, 12.0f, s_ps2Gold);
	EFFe_DrawPs2RectColor(173.0f, 9.0f, 12.0f, 225.0f, s_ps2Gold);
	EFFe_DrawPs2RectColor(1447.0f, 9.0f, 300.0f, 12.0f, s_ps2Gold);
	EFFe_DrawPs2RectColor(1735.0f, 9.0f, 12.0f, 225.0f, s_ps2Gold);
	EFFe_DrawPs2RectColor(173.0f, 878.0f, 300.0f, 12.0f, s_ps2Gold);
	EFFe_DrawPs2RectColor(173.0f, 665.0f, 12.0f, 225.0f, s_ps2Gold);
	EFFe_DrawPs2RectColor(1447.0f, 878.0f, 300.0f, 12.0f, s_ps2Gold);
	EFFe_DrawPs2RectColor(1735.0f, 665.0f, 12.0f, 225.0f, s_ps2Gold);
	EFFe_DrawPs2TextColor(1024.0f, 309.0f, "INSTRUCTIONS", EF_FRONTEND_FONT_BIG, UI_CENTER, s_ps2MapGold, 0.83f, 1.03f);
	EFFe_DrawPs2TextColor(1024.0f, 402.0f, "Use the directional buttons to adjust", EF_FRONTEND_FONT_MEDIUM, UI_CENTER, colorTable[CT_WHITE], 1.29f, 1.55f);
	EFFe_DrawPs2TextColor(1024.0f, 476.0f, "the position of the flashing corner.", EF_FRONTEND_FONT_MEDIUM, UI_CENTER, colorTable[CT_WHITE], 1.29f, 1.55f);
	EFFe_DrawVideoPrompt();
}

static void EFFe_DrawControllerText(float x, float y, const char *text)
{
	EFFe_DrawPs2TextColor(x, y, text, EF_FRONTEND_FONT_BIG, UI_LEFT, colorTable[CT_WHITE], 0.68f, 1.02f);
}

static void EFFe_DrawControllerSmallText(float x, float y, const char *text)
{
	EFFe_DrawPs2TextColor(x, y, text, EF_FRONTEND_FONT_MEDIUM, UI_LEFT, colorTable[CT_WHITE], 0.80f, 0.94f);
}

static void EFFe_DrawControllerAnalogText(float x, float y, const char *text)
{
	EFFe_DrawPs2TextColor(x, y, text, EF_FRONTEND_FONT_BIG, UI_LEFT, colorTable[CT_WHITE], 0.50f, 0.62f);
}

static void EFFe_DrawControllerGoldText(float x, float y, const char *text, int style)
{
	EFFe_DrawPs2TextColor(x, y, text, EF_FRONTEND_FONT_BIG, style, s_ps2MapGold, 0.82f, 1.00f);
}

static void EFFe_DrawControllerGlyph(float x, float y, float w, float h, qhandle_t icon)
{
	if (icon)
	{
		EFFe_DrawPs2Pic(x, y, w, h, icon, CT_WHITE);
	}
}

static void EFFe_DrawControllerBinding(float x, float y, float w, float h, qhandle_t icon, const char *text)
{
	EFFe_DrawControllerGlyph(x, y, w, h, icon);
	EFFe_DrawControllerText(x + 70.0f, y + 7.0f, text);
}

static void EFFe_DrawControllerFrame(void)
{
	EFFe_DrawPs2RectColor(110.0f, 148.0f, 1284.0f, 34.0f, s_ps2MutedPurple);
	EFFe_DrawPs2RectColor(1404.0f, 148.0f, 284.0f, 34.0f, s_ps2MutedPurple);
	EFFe_DrawPs2RectColor(110.0f, 182.0f, 120.0f, 300.0f, s_ps2MutedPurple);
	EFFe_DrawPs2RectColor(110.0f, 488.0f, 120.0f, 349.0f, s_ps2MutedPurple);
	EFFe_DrawPs2PicColor(110.0f, 790.0f, 232.0f, 105.0f, s_assets.ps2ControllerBottomLeftChrome, s_ps2MutedPurple);
	EFFe_DrawPs2RectColor(110.0f, 790.0f, 120.0f, 47.0f, s_ps2MutedPurple);
	EFFe_DrawPs2RectColor(230.0f, 845.0f, 935.0f, 36.0f, s_ps2MutedPurple);
	EFFe_DrawPs2RectColor(1175.0f, 845.0f, 633.0f, 36.0f, s_ps2MutedPurple);
	EFFe_DrawPs2PicColor(1688.0f, 148.0f, 120.0f, 126.0f, s_assets.ps2ControllerTopRightChrome, s_ps2MutedPurple);
	EFFe_DrawPs2RectColor(1688.0f, 246.0f, 120.0f, 180.0f, s_ps2MutedPurple);
	EFFe_DrawPs2RectColor(1688.0f, 432.0f, 120.0f, 449.0f, s_ps2MutedPurple);
}

static void EFFe_DrawControllerScreen(void)
{
	EFFe_DrawPic(0.0f, 0.0f, 640.0f, 480.0f, s_assets.whiteShader, CT_BLACK);
	EFFe_DrawPs2TextColor(155.0f, 68.0f, "ELITE FORCE : CONTROLLER", EF_FRONTEND_FONT_BIG, UI_LEFT, s_ps2Gold, 1.06f, 1.20f);
	EFFe_DrawControllerAcceptCancelPrompt();
	EFFe_DrawControllerFrame();
	EFFe_DrawPs2TextColor(960.0f, 208.0f, "< STANDARD >", EF_FRONTEND_FONT_BIG, UI_CENTER, s_ps2MapGold, 0.86f, 1.00f);

	EFFe_DrawPs2PicSTColor(754.0f, 260.0f, 413.0f, 255.0f, 0.0f, 0.0f, 1.0f, 381.0f / 512.0f,
		s_assets.xboxController, colorTable[CT_WHITE]);

	EFFe_DrawControllerBinding(260.0f, 205.0f, 46.0f, 54.0f, s_assets.xboxA, "JUMP");
	EFFe_DrawControllerBinding(260.0f, 283.0f, 46.0f, 54.0f, s_assets.xboxB, "CROUCH");
	EFFe_DrawControllerBinding(260.0f, 361.0f, 50.0f, 50.0f, s_assets.xboxDpad[0], "ZOOM");
	EFFe_DrawControllerBinding(260.0f, 439.0f, 52.0f, 52.0f, s_assets.xboxBlack, "PREV WEAPON");
	EFFe_DrawControllerBinding(260.0f, 517.0f, 52.0f, 52.0f, s_assets.xboxWhite, "NEXT WEAPON");
	EFFe_DrawControllerBinding(260.0f, 595.0f, 55.0f, 35.0f, s_assets.xboxBack, "DATAPAD");
	EFFe_DrawControllerBinding(260.0f, 673.0f, 55.0f, 35.0f, s_assets.xboxStart, "MENU");

	EFFe_DrawControllerBinding(1260.0f, 205.0f, 58.0f, 43.0f, s_assets.xboxRT, "FIRE");
	EFFe_DrawControllerBinding(1260.0f, 283.0f, 58.0f, 43.0f, s_assets.xboxLT, "ALT. FIRE");
	EFFe_DrawControllerBinding(1260.0f, 361.0f, 46.0f, 54.0f, s_assets.xboxY, "CENTER VIEW");
	EFFe_DrawControllerBinding(1260.0f, 439.0f, 46.0f, 54.0f, s_assets.xboxX, "USE");
	EFFe_DrawControllerBinding(1260.0f, 517.0f, 44.0f, 44.0f, s_assets.xboxLStick, "TOGGLE RUN");
	EFFe_DrawControllerBinding(1260.0f, 595.0f, 44.0f, 44.0f, s_assets.xboxRStick, "TOGGLE VIEW");

	EFFe_DrawControllerGlyph(590.0f, 590.0f, 44.0f, 44.0f, s_assets.xboxLStick);
	EFFe_DrawControllerGlyph(590.0f, 665.0f, 44.0f, 44.0f, s_assets.xboxLStick);
	EFFe_DrawControllerSmallText(645.0f, 555.0f, "Analog Left:");
	EFFe_DrawControllerAnalogText(655.0f, 590.0f, "FORWARD");
	EFFe_DrawControllerAnalogText(655.0f, 628.0f, "BACK");
	EFFe_DrawControllerAnalogText(655.0f, 670.0f, "STEP LEFT");
	EFFe_DrawControllerAnalogText(655.0f, 708.0f, "STEP RIGHT");
	EFFe_DrawControllerGlyph(1005.0f, 590.0f, 44.0f, 44.0f, s_assets.xboxRStick);
	EFFe_DrawControllerGlyph(1005.0f, 665.0f, 44.0f, 44.0f, s_assets.xboxRStick);
	EFFe_DrawControllerSmallText(1060.0f, 555.0f, "Analog Right:");
	EFFe_DrawControllerAnalogText(1070.0f, 590.0f, "LOOK DOWN");
	EFFe_DrawControllerAnalogText(1070.0f, 628.0f, "LOOK UP");
	EFFe_DrawControllerAnalogText(1070.0f, 670.0f, "TURN LEFT");
	EFFe_DrawControllerAnalogText(1070.0f, 708.0f, "TURN RIGHT");
}

static void EFFe_LoadAudioState(void)
{
	if (!s_audioTouched)
	{
		s_audioEffects = 0.5f;
		s_audioMusic = 0.5f;
		s_audioVoice = 1.0f;
		return;
	}

	s_audioEffects = EFFe_Clamp01(ui.Cvar_VariableValue("s_effects_volume"));
	s_audioMusic = EFFe_Clamp01(ui.Cvar_VariableValue("s_music_volume"));
	s_audioVoice = EFFe_Clamp01(ui.Cvar_VariableValue("s_voice_volume"));
}

static void EFFe_ApplyAudioState(void)
{
	ui.Cvar_SetValue("s_effects_volume", s_audioEffects);
	ui.Cvar_SetValue("s_music_volume", s_audioMusic);
	ui.Cvar_SetValue("s_voice_volume", s_audioVoice);
#ifdef _XBOX
	XBLF("STEFX: EF audio settings apply effects=%g music=%g voice=%g", s_audioEffects, s_audioMusic, s_audioVoice);
#endif
}

static void EFFe_AdjustAudio(int direction)
{
	float delta;

	delta = direction > 0 ? 0.1f : -0.1f;
	if (s_cursor == 0)
	{
		s_audioEffects = EFFe_Clamp01(s_audioEffects + delta);
	}
	else if (s_cursor == 1)
	{
		s_audioMusic = EFFe_Clamp01(s_audioMusic + delta);
	}
	else if (s_cursor == 2)
	{
		s_audioVoice = EFFe_Clamp01(s_audioVoice + delta);
	}
	s_audioTouched = qtrue;
	EFFe_ApplyAudioState();
}

static void EFFe_SetNewGameGender(qboolean male)
{
	s_newgameGenderMale = male ? 1 : 0;
	s_newgameGenderTouched = qtrue;
	if (male)
	{
		ui.Cvar_Set("legsmodel", "hazard/default");
		ui.Cvar_Set("torsomodel", "hazard/default");
		ui.Cvar_Set("headmodel", "munro/default");
		ui.Cvar_Set("sex", "male");
	}
	else
	{
		ui.Cvar_Set("legsmodel", "hazardfemale/default");
		ui.Cvar_Set("torsomodel", "hazardfemale/default");
		ui.Cvar_Set("headmodel", "alexandria/default");
		ui.Cvar_Set("sex", "female");
	}
#ifdef _XBOX
	XBLF("STEFX: EF new game gender set male=%d", s_newgameGenderMale);
#endif
}

static void EFFe_SetNewGameDifficulty(int difficulty)
{
	if (difficulty < 0)
	{
		difficulty = 0;
	}
	if (difficulty > 3)
	{
		difficulty = 3;
	}
	s_newgameDifficulty = difficulty;

	if (difficulty == 0)
	{
		ui.Cvar_SetValue("g_spskill", 0.0f);
		ui.Cvar_Set("handicap", "200");
	}
	else if (difficulty == 1)
	{
		ui.Cvar_SetValue("g_spskill", 0.0f);
		ui.Cvar_Set("handicap", "100");
	}
	else if (difficulty == 2)
	{
		ui.Cvar_SetValue("g_spskill", 1.0f);
		ui.Cvar_Set("handicap", "100");
	}
	else
	{
		ui.Cvar_SetValue("g_spskill", 2.0f);
		ui.Cvar_Set("handicap", "100");
	}
#ifdef _XBOX
	XBLF("STEFX: EF new game difficulty set index=%d item='%s'", difficulty, s_newgameItems[difficulty]);
#endif
}

static void EFFe_SyncNewGameState(void)
{
	char sex[32];
	int skill;
	float handicap;

	skill = (int)ui.Cvar_VariableValue("g_spskill");
	handicap = ui.Cvar_VariableValue("handicap");
	if (skill <= 0 && handicap >= 150.0f)
	{
		s_newgameDifficulty = 0;
	}
	else if (skill <= 0)
	{
		s_newgameDifficulty = 1;
	}
	else if (skill == 1)
	{
		s_newgameDifficulty = 2;
	}
	else
	{
		s_newgameDifficulty = 3;
	}

	if (!s_newgameGenderTouched)
	{
		s_newgameGenderMale = 1;
		return;
	}

	ui.Cvar_VariableStringBuffer("sex", sex, sizeof(sex));
	s_newgameGenderMale = Q_stricmp(sex, "female") && Q_stricmp(sex, "f");
}

static void EFFe_StartMap(const char *mapName)
{
#ifdef _XBOX
	XBLF("STEFX: EF new game start map='%s' difficulty=%d genderMale=%d catcher=0x%x", mapName ? mapName : "", s_newgameDifficulty, s_newgameGenderMale, ui.Key_GetCatcher());
	ui.Printf("STEFX_MENU_NEWGAME_START map='%s' cursor=%d difficulty=%d genderMale=%d catcher=0x%x\n",
		mapName ? mapName : "",
		s_cursor,
		s_newgameDifficulty,
		s_newgameGenderMale,
		ui.Key_GetCatcher());
#endif
	EFFe_SetNewGameDifficulty(s_newgameDifficulty);
	EFFe_SetNewGameGender(s_newgameGenderMale ? qtrue : qfalse);
	ui.Cvar_Set("stefx_splitScreen", "0");
	ui.Cvar_Set("stefx_splitScreenPlayers", "1");
	ui.Cvar_Set("stefx_splitScreenMode", "sp");
	ui.Cvar_Set("stefx_splitScreenP2Entity", "-1");
	s_active = qfalse;
	UI_ForceMenuOff();
	ui.Cvar_SetValue("cg_virtualVoyager", 0.0f);
	if (mapName && mapName[0])
	{
		ui.Cmd_ExecuteText(EXEC_APPEND, va("map %s\n", mapName));
	}
}

static void EFFe_SetScreen(efFrontendScreen_t screen, int cursor)
{
	s_screen = screen;
	s_cursor = cursor;
	s_loggedDraw = qfalse;
	if (s_screen == EF_SCREEN_AUDIO)
	{
		EFFe_LoadAudioState();
	}
	else if (s_screen == EF_SCREEN_NEWGAME)
	{
		EFFe_SyncNewGameState();
	}
#ifdef _XBOX
	XBLF("STEFX: EF frontend screen set screen='%s' cursor=%d", EFFe_ScreenName(s_screen), s_cursor);
	ui.Printf("STEFX_MENU_SCREEN_SET screen='%s' cursor=%d catcher=0x%x\n",
		EFFe_ScreenName(s_screen),
		s_cursor,
		ui.Key_GetCatcher());
#endif
}

static void EFFe_OpenScreen(efFrontendScreen_t screen, int cursor, const char *reason)
{
#ifdef _XBOX
	XBLF("STEFX: EF frontend open screen='%s' active=%d reason='%s' realtime=%d catcher=0x%x", EFFe_ScreenName(screen), s_active ? 1 : 0, reason ? reason : "", uis.realtime, ui.Key_GetCatcher());
#endif
	UI_EFQmenu_ClearState(reason ? reason : "ef-frontend-open");
	UI_EFPauseMenu_Deactivate();
	EFFe_Cache();
	ui.Cvar_Set("sv_killserver", "1");
	ui.Key_SetCatcher(KEYCATCH_UI);
	s_active = qtrue;
	EFFe_SetScreen(screen, cursor);
}

static void EFFe_ReturnToMain(void)
{
	EFFe_SetScreen(EF_SCREEN_MAIN, 0);
}

static void EFFe_ReturnToConfigure(void)
{
	EFFe_SetScreen(EF_SCREEN_CONFIGURE, 0);
}

static void EFFe_DrawChildScreen(void)
{
	switch (s_screen)
	{
	case EF_SCREEN_NEWGAME:
		EFFe_DrawNewGameScreen();
		break;
	case EF_SCREEN_LOADGAME:
		EFFe_DrawLoadGameScreen();
		break;
	case EF_SCREEN_CONFIGURE:
		EFFe_DrawConfigureScreen();
		break;
	case EF_SCREEN_AUDIO:
		EFFe_DrawAudioScreen();
		break;
	case EF_SCREEN_VIDEO:
		EFFe_DrawVideoScreen();
		break;
	case EF_SCREEN_CONTROLLER:
		EFFe_DrawControllerScreen();
		break;
	case EF_SCREEN_STUB:
		EFFe_DrawStubScreen();
		break;
	default:
		break;
	}
}
static void EFFe_DrawFrame(void)
{
	EFFe_DrawMainTopChrome("ELITE FORCE : MAIN MENU", qfalse);

	EFFe_DrawPanelCode(359.0f, 301.0f, "81453");
	EFFe_DrawPanelCode(359.0f, 351.0f, "9343");
	EFFe_DrawPanelCode(359.0f, 744.0f, "431108");

	EFFe_DrawPs2PanelBracket(916.0f, 250.0f, 63.0f, qfalse, qfalse);
	EFFe_DrawPs2PanelBracket(916.0f, 619.0f, 63.0f, qfalse, qtrue);
	EFFe_DrawPs2PanelBracket(1655.0f, 251.0f, 66.0f, qtrue, qfalse);
	EFFe_DrawPs2PanelBracket(1655.0f, 619.0f, 66.0f, qtrue, qtrue);
	EFFe_DrawPs2Pic(970.0f, 253.0f, 700.0f, 607.0f, s_assets.quadrants, CT_WHITE);
	EFFe_DrawPs2RectColor(918.0f, 542.0f, 803.0f, 4.0f, s_ps2LightBrown);
	EFFe_DrawPs2RectColor(1342.0f, 253.0f, 4.0f, 607.0f, s_ps2LightBrown);

	EFFe_DrawPs2TextColor(1082.0f, 306.0f, "Dominion", EF_FRONTEND_FONT_MEDIUM, UI_LEFT, s_ps2MapGold, 0.96f, 1.06f);
	EFFe_DrawPs2TextColor(997.0f, 371.0f, "Bajoran", EF_FRONTEND_FONT_MEDIUM, UI_LEFT, s_ps2MapGold, 0.96f, 1.06f);
	EFFe_DrawPs2TextColor(997.0f, 417.0f, "Wormhole", EF_FRONTEND_FONT_MEDIUM, UI_LEFT, s_ps2MapGold, 0.96f, 1.06f);
	EFFe_DrawPs2TextColor(1522.0f, 315.0f, "Voyager", EF_FRONTEND_FONT_MEDIUM, UI_LEFT, s_ps2MapGold, 0.96f, 1.06f);
	EFFe_DrawPs2TextColor(1375.0f, 372.0f, "Borg", EF_FRONTEND_FONT_MEDIUM, UI_LEFT, s_ps2MapGold, 0.96f, 1.06f);
	EFFe_DrawPs2TextColor(1375.0f, 418.0f, "Space", EF_FRONTEND_FONT_MEDIUM, UI_LEFT, s_ps2MapGold, 0.96f, 1.06f);
	EFFe_DrawPs2TextColor(970.0f, 519.0f, "Gamma", EF_FRONTEND_FONT_MEDIUM, UI_LEFT, s_ps2MapGold, 0.96f, 1.06f);
	EFFe_DrawPs2TextColor(970.0f, 569.0f, "Alpha", EF_FRONTEND_FONT_MEDIUM, UI_LEFT, s_ps2MapGold, 0.96f, 1.06f);
	EFFe_DrawPs2TextColor(1575.0f, 518.0f, "Delta", EF_FRONTEND_FONT_MEDIUM, UI_LEFT, s_ps2MapGold, 0.96f, 1.06f);
	EFFe_DrawPs2TextColor(1592.0f, 567.0f, "Beta", EF_FRONTEND_FONT_MEDIUM, UI_LEFT, s_ps2MapGold, 0.96f, 1.06f);
	EFFe_DrawPs2TextColor(1010.0f, 661.0f, "Ferengi Alliance", EF_FRONTEND_FONT_MEDIUM, UI_LEFT, s_ps2MapGold, 0.96f, 1.06f);
	EFFe_DrawPs2TextColor(970.0f, 715.0f, "Cardassia", EF_FRONTEND_FONT_MEDIUM, UI_LEFT, s_ps2MapGold, 0.96f, 1.06f);
	EFFe_DrawPs2TextColor(970.0f, 770.0f, "Federation", EF_FRONTEND_FONT_MEDIUM, UI_LEFT, s_ps2MapGold, 0.96f, 1.06f);
	EFFe_DrawPs2TextColor(1534.0f, 648.0f, "Romulan", EF_FRONTEND_FONT_MEDIUM, UI_LEFT, s_ps2MapGold, 0.96f, 1.06f);
	EFFe_DrawPs2TextColor(1534.0f, 700.0f, "Empire", EF_FRONTEND_FONT_MEDIUM, UI_LEFT, s_ps2MapGold, 0.96f, 1.06f);
	EFFe_DrawPs2TextColor(1534.0f, 741.0f, "Klingon", EF_FRONTEND_FONT_MEDIUM, UI_LEFT, s_ps2MapGold, 0.96f, 1.06f);
	EFFe_DrawPs2TextColor(1534.0f, 793.0f, "Empire", EF_FRONTEND_FONT_MEDIUM, UI_LEFT, s_ps2MapGold, 0.96f, 1.06f);
}

static void EFFe_ActivateButton(const efFrontendButton_t *button)
{
	if (!button || !button->enabled)
	{
		return;
	}

#ifdef _XBOX
	XBLF("STEFX: EF frontend activate label='%s' command='%s' catcher=0x%x",
		EFFe_ButtonText(button, 0),
		button->commandName ? button->commandName : "",
		ui.Key_GetCatcher());
#endif

	if (!button->commandName || !button->commandName[0])
	{
		return;
	}

	if (!UI_EFQmenu_ConsoleCommand(button->commandName))
	{
#ifdef _XBOX
		XBLF("STEFX: EF frontend command unhandled label='%s' command='%s'",
			EFFe_ButtonText(button, 0),
			button->commandName);
#endif
		ui.Key_SetCatcher(KEYCATCH_UI);
	}
}
static void EFFe_MoveCursor(int count, int delta)
{
	if (count <= 0)
	{
		return;
	}
	s_cursor = (s_cursor + count + delta) % count;
}

static void EFFe_HandleMainKey(int key)
{
	if (EFFe_IsUpKey(key))
	{
		do
		{
			EFFe_MoveCursor(EF_FRONTEND_BUTTON_COUNT, -1);
		}
		while (!s_buttons[s_cursor].enabled);
	}
	else if (EFFe_IsDownKey(key))
	{
		do
		{
			EFFe_MoveCursor(EF_FRONTEND_BUTTON_COUNT, 1);
		}
		while (!s_buttons[s_cursor].enabled);
	}
	else if (EFFe_IsAcceptKey(key))
	{
		if (s_buttons[s_cursor].enabled)
		{
#ifdef _XBOX
			XBLF("STEFX: EF frontend command label='%s' command='%s'", EFFe_ButtonText(&s_buttons[s_cursor], 0), s_buttons[s_cursor].commandName);
			ui.Printf("STEFX_MENU_MAIN_ACCEPT cursor=%d label='%s' command='%s'\n",
				s_cursor,
				EFFe_ButtonText(&s_buttons[s_cursor], 0),
				s_buttons[s_cursor].commandName ? s_buttons[s_cursor].commandName : "");
#endif
			EFFe_ActivateButton(&s_buttons[s_cursor]);
		}
	}
}

static void EFFe_HandleNewGameKey(int key)
{
	if (EFFe_IsBackKey(key))
	{
		EFFe_ReturnToMain();
		return;
	}
	if (EFFe_IsUpKey(key))
	{
		EFFe_MoveCursor(EF_FRONTEND_NEWGAME_COUNT, -1);
		return;
	}
	if (EFFe_IsDownKey(key))
	{
		EFFe_MoveCursor(EF_FRONTEND_NEWGAME_COUNT, 1);
		return;
	}
	if (EFFe_IsLeftKey(key) || EFFe_IsRightKey(key))
	{
		if (s_cursor <= 3)
		{
			EFFe_SetNewGameDifficulty(s_cursor);
		}
		else if (s_cursor == 4 || s_cursor == 5)
		{
			EFFe_SetNewGameGender(s_cursor == 5 ? qtrue : qfalse);
		}
		return;
	}
	if (!EFFe_IsAcceptKey(key))
	{
		return;
	}

#ifdef _XBOX
	ui.Printf("STEFX_MENU_NEWGAME_ACCEPT cursor=%d item='%s'\n",
		s_cursor,
		(s_cursor >= 0 && s_cursor < EF_FRONTEND_NEWGAME_COUNT) ? s_newgameItems[s_cursor] : "<bad>");
#endif

	if (s_cursor <= 3)
	{
		EFFe_SetNewGameDifficulty(s_cursor);
	}
	else if (s_cursor == 4 || s_cursor == 5)
	{
		EFFe_SetNewGameGender(s_cursor == 5 ? qtrue : qfalse);
	}
	else if (s_cursor == 6)
	{
		EFFe_StartMap("tutorial");
	}
	else
	{
		EFFe_StartMap("borg1");
	}
}

static void EFFe_HandleConfigureKey(int key)
{
	if (EFFe_IsBackKey(key))
	{
		EFFe_ReturnToMain();
		return;
	}
	if (EFFe_IsUpKey(key))
	{
		EFFe_MoveCursor(EF_FRONTEND_CONFIGURE_COUNT, -1);
		return;
	}
	if (EFFe_IsDownKey(key))
	{
		EFFe_MoveCursor(EF_FRONTEND_CONFIGURE_COUNT, 1);
		return;
	}
	if (!EFFe_IsAcceptKey(key))
	{
		return;
	}

#ifdef _XBOX
	XBLF("STEFX: EF configure activate index=%d item='%s'", s_cursor, s_configureItems[s_cursor]);
#endif
	if (s_cursor == 0)
	{
		EFFe_SetScreen(EF_SCREEN_AUDIO, 0);
	}
	else if (s_cursor == 1)
	{
		EFFe_SetScreen(EF_SCREEN_VIDEO, 0);
	}
	else
	{
		EFFe_SetScreen(EF_SCREEN_CONTROLLER, 0);
	}
}

static void EFFe_HandleAudioKey(int key)
{
	if (EFFe_IsBackKey(key) || EFFe_IsAcceptKey(key))
	{
		EFFe_ApplyAudioState();
		EFFe_ReturnToConfigure();
		return;
	}
	if (EFFe_IsUpKey(key))
	{
		EFFe_MoveCursor(4, -1);
		return;
	}
	if (EFFe_IsDownKey(key))
	{
		EFFe_MoveCursor(4, 1);
		return;
	}
	if (EFFe_IsLeftKey(key))
	{
		EFFe_AdjustAudio(-1);
		return;
	}
	if (EFFe_IsRightKey(key))
	{
		EFFe_AdjustAudio(1);
		return;
	}
}

static void EFFe_HandleVideoKey(int key)
{
	if (key == A_JOY16)
	{
		s_videoCorner = (s_videoCorner + 1) & 3;
#ifdef _XBOX
		XBLF("STEFX: EF video screen-size switch corner=%d", s_videoCorner);
#endif
		return;
	}
	if (key == A_JOY14)
	{
		s_videoCorner = 0;
#ifdef _XBOX
		XBLF("STEFX: EF video screen-size default corner=%d", s_videoCorner);
#endif
		return;
	}
	if (EFFe_IsBackKey(key) || EFFe_IsAcceptKey(key))
	{
#ifdef _XBOX
		XBLF("STEFX: EF video screen-size close key=%d corner=%d", key, s_videoCorner);
#endif
		EFFe_ReturnToConfigure();
	}
}

static void EFFe_HandleControllerKey(int key)
{
	if (EFFe_IsBackKey(key) || EFFe_IsAcceptKey(key))
	{
#ifdef _XBOX
		XBLF("STEFX: EF controller menu close key=%d", key);
#endif
		EFFe_ReturnToConfigure();
	}
}

static void EFFe_RunMenuSmoke(int realtime)
{
#ifdef _XBOX
	char target[32];
	int desiredCursor;
	int newGameCursor;
	qboolean targetHolomatch;

	ui.Cvar_VariableStringBuffer("stefx_menu_smoke", target, sizeof(target));
	if (!target[0] || !Q_stricmp(target, "0"))
	{
		s_menuSmokeStage = 0;
		s_menuSmokeTarget[0] = '\0';
		s_menuSmokeMainDownRemaining = 0;
		return;
	}

	targetHolomatch = (qboolean)!Q_stricmp(target, "holomatch");
	desiredCursor = targetHolomatch ? 2 : 0;
	newGameCursor = !Q_stricmp(target, "tutorial") ? 6 : (!Q_stricmp(target, "engage") ? 7 : 0);
	if (Q_stricmp(target, "tutorial") && Q_stricmp(target, "engage") && !targetHolomatch)
	{
		ui.Printf("STEFX_MENU_SMOKE invalid target='%s'\n", target);
		ui.Cvar_Set("stefx_menu_smoke", "0");
		s_menuSmokeStage = 0;
		s_menuSmokeTarget[0] = '\0';
		s_menuSmokeMainDownRemaining = 0;
		return;
	}

	if (!s_menuSmokeStage || Q_stricmp(target, s_menuSmokeTarget))
	{
		Q_strncpyz(s_menuSmokeTarget, target, sizeof(s_menuSmokeTarget));
		s_menuSmokeStage = 1;
		s_menuSmokeNextRealtime = realtime + 500;
		s_menuSmokeMainDownRemaining = desiredCursor;
		s_menuSmokeDownRemaining = newGameCursor;
		ui.Printf("STEFX_MENU_SMOKE begin target='%s' mainCursor=%d subCursor=%d screen='%s' cursor=%d realtime=%d\n",
			s_menuSmokeTarget,
			desiredCursor,
			newGameCursor,
			EFFe_ScreenName(s_screen),
			s_cursor,
			realtime);
		return;
	}

	if (realtime < s_menuSmokeNextRealtime)
	{
		return;
	}

	if (s_menuSmokeStage == 1)
	{
		if (s_screen != EF_SCREEN_MAIN)
		{
			ui.Printf("STEFX_MENU_SMOKE wait-main screen='%s' cursor=%d realtime=%d\n",
				EFFe_ScreenName(s_screen),
				s_cursor,
				realtime);
			s_menuSmokeNextRealtime = realtime + 250;
			return;
		}
		if (s_menuSmokeMainDownRemaining > 0)
		{
			ui.Printf("STEFX_MENU_SMOKE main-down remainingBefore=%d cursor=%d realtime=%d\n",
				s_menuSmokeMainDownRemaining,
				s_cursor,
				realtime);
			EFFe_HandleMainKey(A_CURSOR_DOWN);
			s_menuSmokeMainDownRemaining--;
			s_menuSmokeNextRealtime = realtime + 160;
			return;
		}
		if (s_cursor != desiredCursor)
		{
			ui.Printf("STEFX_MENU_SMOKE main-align target='%s' cursorBefore=%d cursorAfter=%d realtime=%d\n",
				s_menuSmokeTarget,
				s_cursor,
				desiredCursor,
				realtime);
			s_cursor = desiredCursor;
		}
		ui.Printf("STEFX_MENU_SMOKE main-accept target='%s' cursor=%d realtime=%d\n", s_menuSmokeTarget, s_cursor, realtime);
		EFFe_HandleMainKey(A_ENTER);
		if (!Q_stricmp(s_menuSmokeTarget, "holomatch"))
		{
			ui.Cvar_Set("stefx_menu_smoke", "0");
			s_menuSmokeStage = 0;
			s_menuSmokeTarget[0] = '\0';
			s_menuSmokeMainDownRemaining = 0;
			return;
		}
		s_menuSmokeStage = 2;
		s_menuSmokeNextRealtime = realtime + 500;
		return;
	}

	if (s_menuSmokeStage == 2)
	{
		if (s_screen != EF_SCREEN_NEWGAME)
		{
			ui.Printf("STEFX_MENU_SMOKE wait-newgame screen='%s' cursor=%d realtime=%d\n",
				EFFe_ScreenName(s_screen),
				s_cursor,
				realtime);
			s_menuSmokeNextRealtime = realtime + 250;
			return;
		}
		if (s_menuSmokeDownRemaining > 0)
		{
			ui.Printf("STEFX_MENU_SMOKE newgame-down remainingBefore=%d cursor=%d realtime=%d\n",
				s_menuSmokeDownRemaining,
				s_cursor,
				realtime);
			EFFe_HandleNewGameKey(A_CURSOR_DOWN);
			s_menuSmokeDownRemaining--;
			s_menuSmokeNextRealtime = realtime + 160;
			return;
		}
		if (s_cursor != newGameCursor)
		{
			ui.Printf("STEFX_MENU_SMOKE newgame-align target='%s' cursorBefore=%d cursorAfter=%d realtime=%d\n",
				s_menuSmokeTarget,
				s_cursor,
				newGameCursor,
				realtime);
			s_cursor = newGameCursor;
		}
		s_menuSmokeStage = 3;
		s_menuSmokeNextRealtime = realtime + 250;
		return;
	}

	if (s_menuSmokeStage == 3)
	{
		if (s_screen != EF_SCREEN_NEWGAME)
		{
			ui.Printf("STEFX_MENU_SMOKE abort-before-accept screen='%s' cursor=%d realtime=%d\n",
				EFFe_ScreenName(s_screen),
				s_cursor,
				realtime);
			ui.Cvar_Set("stefx_menu_smoke", "0");
			s_menuSmokeStage = 0;
			s_menuSmokeMainDownRemaining = 0;
			return;
		}
		ui.Printf("STEFX_MENU_SMOKE newgame-accept target='%s' cursor=%d item='%s' realtime=%d\n",
			s_menuSmokeTarget,
			s_cursor,
			(s_cursor >= 0 && s_cursor < EF_FRONTEND_NEWGAME_COUNT) ? s_newgameItems[s_cursor] : "<bad>",
			realtime);
		EFFe_HandleNewGameKey(A_ENTER);
		ui.Cvar_Set("stefx_menu_smoke", "0");
		s_menuSmokeStage = 0;
		s_menuSmokeTarget[0] = '\0';
		s_menuSmokeMainDownRemaining = 0;
	}
#else
	(void)realtime;
#endif
}

qboolean UI_EFMainMenu_IsActive(void)
{
	return s_active;
}

void UI_EFMainMenu_Open(void)
{
	EFFe_OpenScreen(EF_SCREEN_MAIN, 0, "ef-main-open");
#ifdef _XBOX
	ui.Printf("STEFX: EF main menu open realtime=%d catcher=0x%x\n", uis.realtime, ui.Key_GetCatcher());
#endif
}

void UI_EFMainMenu_OpenNewGame(void)
{
	EFFe_OpenScreen(EF_SCREEN_NEWGAME, 0, "ef-newgame-open");
}

void UI_EFMainMenu_OpenLoadGame(void)
{
	EFFe_OpenScreen(EF_SCREEN_LOADGAME, 0, "ef-loadgame-open");
}

void UI_EFMainMenu_OpenConfigure(void)
{
	EFFe_OpenScreen(EF_SCREEN_CONFIGURE, 0, "ef-configure-open");
}

void UI_EFMainMenu_OpenAudio(void)
{
	EFFe_OpenScreen(EF_SCREEN_AUDIO, 0, "ef-audio-open");
}

void UI_EFMainMenu_OpenVideo(void)
{
	EFFe_OpenScreen(EF_SCREEN_VIDEO, 0, "ef-video-open");
}

void UI_EFMainMenu_OpenController(void)
{
	EFFe_OpenScreen(EF_SCREEN_CONTROLLER, 0, "ef-controller-open");
}

void UI_EFMainMenu_StartSplitScreenBaseline(void)
{
#ifdef _XBOX
	char splitValue[16];
	char playersValue[16];
	XBLF("STEFX: EF split-screen coop start map='%s' players=2 catcher=0x%x", EF_SPLITSCREEN_BASELINE_MAP, ui.Key_GetCatcher());
	ui.Printf("STEFX_MENU_SPLITSCREEN_START map='%s' players=2 cursor=%d catcher=0x%x\n",
		EF_SPLITSCREEN_BASELINE_MAP,
		s_cursor,
		ui.Key_GetCatcher());
#endif
	ui.Cvar_Set("stefx_splitScreen", "1");
	ui.Cvar_Set("stefx_splitScreenPlayers", "2");
	ui.Cvar_Set("stefx_splitScreenMode", "coop");
	ui.Cvar_Set("stefx_splitScreenP2Entity", "-1");
#ifdef _XBOX
	ui.Cvar_VariableStringBuffer("stefx_splitScreen", splitValue, sizeof(splitValue));
	ui.Cvar_VariableStringBuffer("stefx_splitScreenPlayers", playersValue, sizeof(playersValue));
	ui.Printf("STEFX_MENU_SPLITSCREEN_CVARS split='%s' players='%s'\n", splitValue, playersValue);
#endif
	s_active = qfalse;
	UI_ForceMenuOff();
	ui.Cvar_SetValue("cg_virtualVoyager", 0.0f);
	ui.Cmd_ExecuteText(EXEC_APPEND, "map " EF_SPLITSCREEN_BASELINE_MAP "\n");
}

void UI_EFMainMenu_OpenStub(const char *title, const char *line)
{
	Q_strncpyz(s_stubTitle, title ? title : "ELITE FORCE", sizeof(s_stubTitle));
	Q_strncpyz(s_stubLine, line ? line : "THIS MENU IS NOT AVAILABLE YET", sizeof(s_stubLine));
	EFFe_OpenScreen(EF_SCREEN_STUB, 0, "ef-stub-open");
}

void UI_EFMainMenu_Deactivate(void)
{
	if (!s_active)
	{
		return;
	}

	s_active = qfalse;
	s_screen = EF_SCREEN_MAIN;
#ifdef _XBOX
	XBLF("STEFX: EF frontend deactivate catcher=0x%x", ui.Key_GetCatcher());
	ui.Printf("STEFX: EF frontend deactivate catcher=0x%x\n", ui.Key_GetCatcher());
#endif
}

void UI_EFMainMenu_Draw(int realtime)
{
	int i;
	static unsigned int s_drawFrames = 0;

	if (!s_active)
	{
		return;
	}

	if (s_screen == EF_SCREEN_MAIN)
	{
		EFFe_DrawFrame();
		for (i = 0; i < EF_FRONTEND_BUTTON_COUNT; i++)
		{
			EFFe_DrawButton(&s_buttons[i], i);
		}
	}
	else
	{
		EFFe_DrawChildScreen();
	}

	if (!s_loggedDraw)
	{
#ifdef _XBOX
		XBLF("STEFX: EF frontend first draw screen='%s' realtime=%d cursor=%d", EFFe_ScreenName(s_screen), realtime, s_cursor);
		ui.Printf("STEFX: EF frontend first draw screen='%s' realtime=%d cursor=%d\n", EFFe_ScreenName(s_screen), realtime, s_cursor);
#endif
		s_loggedDraw = qtrue;
	}
	EFFe_RunMenuSmoke(realtime);
	++s_drawFrames;
	if ((s_drawFrames % 300) == 0)
	{
#ifdef _XBOX
		XBLF("STEFX: EF frontend heartbeat screen='%s' frames=%u realtime=%d cursor=%d", EFFe_ScreenName(s_screen), s_drawFrames, realtime, s_cursor);
		ui.Printf("STEFX: EF frontend heartbeat screen='%s' frames=%u realtime=%d cursor=%d\n", EFFe_ScreenName(s_screen), s_drawFrames, realtime, s_cursor);
#endif
	}
}

void UI_EFMainMenu_KeyEvent(int key, qboolean down)
{
	if (!s_active || !down)
	{
		return;
	}

#ifdef _XBOX
	ui.Printf("STEFX_MENU_KEY screen='%s' key=%d cursorBefore=%d catcher=0x%x\n",
		EFFe_ScreenName(s_screen),
		key,
		s_cursor,
		ui.Key_GetCatcher());
#endif

	switch (s_screen)
	{
	case EF_SCREEN_MAIN:
		EFFe_HandleMainKey(key);
		break;
	case EF_SCREEN_NEWGAME:
		EFFe_HandleNewGameKey(key);
		break;
	case EF_SCREEN_LOADGAME:
		if (EFFe_IsAcceptKey(key) || EFFe_IsBackKey(key))
		{
			EFFe_ReturnToMain();
		}
		break;
	case EF_SCREEN_CONFIGURE:
		EFFe_HandleConfigureKey(key);
		break;
	case EF_SCREEN_AUDIO:
		EFFe_HandleAudioKey(key);
		break;
	case EF_SCREEN_VIDEO:
		EFFe_HandleVideoKey(key);
		break;
	case EF_SCREEN_CONTROLLER:
		EFFe_HandleControllerKey(key);
		break;
	case EF_SCREEN_STUB:
		if (EFFe_IsAcceptKey(key) || EFFe_IsBackKey(key))
		{
			EFFe_ReturnToMain();
		}
		break;
	default:
		break;
	}

#ifdef _XBOX
	ui.Printf("STEFX_MENU_KEY_DONE screen='%s' key=%d cursorAfter=%d catcher=0x%x active=%d\n",
		EFFe_ScreenName(s_screen),
		key,
		s_cursor,
		ui.Key_GetCatcher(),
		s_active ? 1 : 0);
#endif
}
