#ifndef STEFX_UI_EF_QMENU_SHARED_H
#define STEFX_UI_EF_QMENU_SHARED_H

//
// Shared Elite Force qmenu model.  Holomatch MP consumes these exact SP/EF
// structures through its syscall bridge; menu behavior remains in code/ui.
//

#ifndef MAX_EDIT_LINE
#define MAX_EDIT_LINE 256
#endif

#define MAX_MENUDEPTH 8
#define MAX_QMENUITEMS 64

#define MTYPE_SLIDER 0
#define MTYPE_LIST 1
#define MTYPE_ACTION 2
#define MTYPE_SPINCONTROL 3
#define MTYPE_SEPARATOR 4
#define MTYPE_FIELD 5
#define MTYPE_RADIOBUTTON 6
#define MTYPE_BITMAP 7
#define MTYPE_TEXT 8
#define MTYPE_SCROLLLIST 9

#define QMF_LEFT_JUSTIFY 0x00000001
#define QMF_GRAYED 0x00000002
#define QMF_NUMBERSONLY 0x00000004
#define QMF_HIGHLIGHT_IF_FOCUS 0x00000008
#define QMF_BLINK 0x00000010
#define QMF_CENTER_JUSTIFY 0x00000020
#define QMF_RIGHT_JUSTIFY 0x00000040
#define QMF_HASMOUSEFOCUS 0x00000080
#define QMF_OWNERDRAW 0x00000100
#define QMF_RBONOFFSTYLE 0x00000200
#define QMF_HIGHLIGHT 0x00000400
#define QMF_MOUSEONLY 0x00000800
#define QMF_HIDDEN 0x00001000
#define QMF_SMALLFONT 0x00002000
#define QMF_INACTIVE 0x00004000
#define QMF_HIGHLIGHTIFFOCUS2 0x00008000

#define QM_GOTFOCUS 1
#define QM_LOSTFOCUS 2
#define QM_ACTIVATED 3

#ifndef ITEM_TEXTSTYLE_NORMAL
#define ITEM_TEXTSTYLE_NORMAL 0
#define ITEM_TEXTSTYLE_BLINK 1
#define ITEM_TEXTSTYLE_PULSE 2
#define ITEM_TEXTSTYLE_SHADOWED 3
#define ITEM_TEXTSTYLE_OUTLINED 4
#define ITEM_TEXTSTYLE_OUTLINESHADOWED 5
#define ITEM_TEXTSTYLE_SHADOWEDMORE 6
#endif

#ifndef STYLE_DROPSHADOW
#define STYLE_DROPSHADOW 0x80000000
#endif

#ifndef STYLE_BLINK
#define STYLE_BLINK 0x40000000
#endif

#ifndef UI_GIANTFONT
#define UI_GIANTFONT 0x00000040
#endif

#ifndef UI_TINYFONT
#define UI_TINYFONT 0x00010000
#endif

typedef struct {
	int cursor;
	int scroll;
	int widthInChars;
	char buffer[MAX_EDIT_LINE];
	int maxchars;
	int style;
	int textEnum;
	int textcolor;
	int textcolor2;
} uifield_t;

typedef struct _tag_menuframework
{
	const char *title;
	int cursor;
	int cursor_prev;
	int nitems;
	void *items[MAX_QMENUITEMS];
	const char *statusbar;
	void (*opening)(void);
	void (*closing)(void);
	void (*draw)(void);
	sfxHandle_t (*key)(int key);
	qboolean wrapAround;
	qboolean fullscreen;
	qboolean initialized;
	float openingStart;
	float closingStart;
	float subSeqStatus[8];
	int cnt;
	int descX;
	int descY;
	int listX;
	int listY;
	int titleX;
	int titleY;
	int titleI;
	int footNoteEnum;
} menuframework_s;

typedef struct
{
	int type;
	const char *name;
	int id;
	int x;
	int y;
	int left;
	int top;
	int right;
	int bottom;
	menuframework_s *parent;
	int menuPosition;
	unsigned flags;
	const char *statusbar;
	void (*callback)(void *self, int notification);
	void (*statusbarfunc)(void *self);
	void (*ownerdraw)(void *self);
} menucommon_s;

typedef struct
{
	menucommon_s generic;
	uifield_t field;
} menufield_s;

typedef struct
{
	menucommon_s generic;
	float minvalue;
	float maxvalue;
	float curvalue;
	int focusWidth;
	int focusHeight;
	int color;
	int color2;
	int shader;
	int width;
	int height;
	char *thumbName;
	int thumbShader;
	int thumbWidth;
	int thumbHeight;
	int thumbColor;
	int thumbColor2;
	int thumbGraphicWidth;
	char *picName;
	int picShader;
	int picWidth;
	int picHeight;
	int picX;
	int picY;
	int textX;
	int textY;
	int textEnum;
	int textcolor;
	int textcolor2;
	float range;
} menuslider_s;

typedef struct
{
	menucommon_s generic;
	int oldvalue;
	int curvalue;
	int numitems;
	int top;
	const char **itemnames;
	int *listnames;
	int width;
	int height;
	int columns;
	int seperation;
	int color;
	int color2;
	int textEnum;
	int textX;
	int textY;
	int textcolor;
	int textcolor2;
	byte updated;
} menulist_s;

typedef struct
{
	menucommon_s generic;
	int color;
	int color2;
	int color3;
	int textEnum;
	int textEnum2;
	int textX;
	int textY;
	int textcolor;
	int textcolor2;
	int textcolor3;
	int width;
	int height;
	byte updated;
} menuaction_s;

typedef struct
{
	menucommon_s generic;
} menuseparator_s;

typedef struct
{
	menucommon_s generic;
	int curvalue;
} menuradiobutton_s;

typedef struct
{
	menucommon_s generic;
	char *focuspic;
	int shader;
	int focusshader;
	int focusX;
	int focusY;
	int focusWidth;
	int focusHeight;
	int width;
	int height;
	int color;
	int color2;
	char *textPtr;
	int textEnum;
	int textEnum2;
	int textX;
	int textY;
	int textcolor;
	int textcolor2;
	int textStyle;
} menubitmap_s;

typedef struct
{
	menucommon_s generic;
	int type;
	int width;
	int height;
	int color;
	int color2;
	int textEnum;
	int textX;
	int textY;
	int textcolor;
	int textcolor2;
} menubutton_s;

typedef struct
{
	menucommon_s generic;
	char *string;
	int normaltextEnum;
	int buttontextEnum;
	int normaltextEnum2;
	int buttontextEnum2;
	int normaltextEnum3;
	int buttontextEnum3;
	int style;
	int color;
	int color2;
	int focusX;
	int focusY;
	int focusWidth;
	int focusHeight;
} menutext_s;

#endif
