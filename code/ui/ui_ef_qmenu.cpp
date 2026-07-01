// leave this at the top of all UI_xxxx files for PCH reasons.
#include "../server/exe_headers.h"

#include "ui_local.h"
#include "menudef.h"
#ifdef _XBOX
#include <xtl.h>
#include "../win32/xb_log.h"
#endif

extern void Text_Paint(float x, float y, float scale, vec4_t color, const char *text, int iMaxPixelWidth, int style, int iFontIndex);

enum
{
	EFQ_ID_STUB_RETURN = 100
};

static menuframework_s s_stubMenu;
static menuaction_s s_stubButtons[1];
static char s_stubTitle[64];
static char s_stubLine1[96];
static char s_stubLine2[96];
static qboolean s_stubReturnMain = qtrue;
static int EFQ_Font(void)
{
	if (uiInfo.uiDC.Assets.qhMediumFont)
	{
		return uiInfo.uiDC.Assets.qhMediumFont;
	}
	return UI_RegisterFont("ergoec");
}

static int EFQ_TextWidth(const char *text, float scale)
{
	if (!text || !text[0])
	{
		return 0;
	}
	return ui.R_Font_StrLenPixels(text, EFQ_Font(), scale);
}

static void EFQ_DrawText(float x, float y, const char *text, int style, int colorIndex)
{
	float scale = 0.6f;

	if (!text || !text[0])
	{
		return;
	}

	if (style & UI_TINYFONT)
	{
		scale = 0.45f;
	}
	else if (style & UI_BIGFONT)
	{
		scale = 0.9f;
	}

	if (style & UI_CENTER)
	{
		x -= EFQ_TextWidth(text, scale) * 0.5f;
	}
	else if (style & UI_RIGHT)
	{
		x -= EFQ_TextWidth(text, scale);
	}

	Text_Paint(x, y, scale, colorTable[colorIndex], text, 0, ITEM_TEXTSTYLE_NORMAL, EFQ_Font());
}

static void EFQ_InitAction(menuaction_s *action, int id, int x, int y, int w, int h, const char *label, void (*callback)(void *, int))
{
	memset(action, 0, sizeof(*action));
	action->generic.type = MTYPE_ACTION;
	action->generic.flags = QMF_HIGHLIGHT_IF_FOCUS;
	action->generic.x = x;
	action->generic.y = y;
	action->generic.name = label;
	action->generic.id = id;
	action->generic.callback = callback;
	action->width = w;
	action->height = h;
	action->color = CT_DKPURPLE1;
	action->color2 = CT_LTPURPLE1;
	action->textX = 5;
	action->textY = 1;
	action->textcolor = CT_BLACK;
	action->textcolor2 = CT_WHITE;
}

static const char *EFQ_ItemText(menucommon_s *item)
{
	if (!item)
	{
		return NULL;
	}

	if (item->type == MTYPE_BITMAP)
	{
		menubitmap_s *bitmap = (menubitmap_s *)item;
		if (bitmap->textPtr && bitmap->textPtr[0])
		{
			return bitmap->textPtr;
		}
	}
	else if (item->type == MTYPE_TEXT)
	{
		menutext_s *text = (menutext_s *)item;
		if (text->string && text->string[0])
		{
			return text->string;
		}
	}

	return item->name;
}

static qboolean EFQ_ItemSelectable(menucommon_s *item)
{
	if (!item)
	{
		return qfalse;
	}

	if (item->type == MTYPE_SEPARATOR)
	{
		return qfalse;
	}

	if (item->flags & (QMF_GRAYED | QMF_HIDDEN | QMF_MOUSEONLY | QMF_INACTIVE))
	{
		return qfalse;
	}

	return qtrue;
}

static void EFQ_SetBounds(menucommon_s *item, int width, int height)
{
	if (!item)
	{
		return;
	}

	item->left = item->x;
	item->top = item->y;
	item->right = item->x + width;
	item->bottom = item->y + height;
}

qboolean UI_EFQmenu_IsActive(void)
{
	return uis.activemenu != NULL;
}

void UI_EFQmenu_ClearState(const char *reason)
{
#ifdef _XBOX
	XBLF("STEFX: EF qmenu clear reason='%s' depth=%d active=%p catcher=0x%x",
		reason ? reason : "",
		uis.menusp,
		(void*)uis.activemenu,
		ui.Key_GetCatcher());
#endif
	uis.menusp = 0;
	uis.activemenu = NULL;
	memset(uis.stack, 0, sizeof(uis.stack));
	uis.firstdraw = qtrue;
}

void UI_PushMenu(menuframework_s *menu)
{
	int i;

	if (!menu)
	{
		return;
	}

	for (i = 0; i < uis.menusp; i++)
	{
		if (uis.stack[i] == menu)
		{
			uis.menusp = i;
			break;
		}
	}

	if (i == uis.menusp)
	{
		if (uis.menusp >= MAX_MENUDEPTH)
		{
			ui.Error(ERR_FATAL, "UI_PushMenu: EF qmenu stack overflow");
		}
		uis.stack[uis.menusp++] = menu;
	}

	uis.activemenu = menu;
	menu->cursor = 0;
	menu->cursor_prev = 0;
	ui.Key_SetCatcher(KEYCATCH_UI);

	for (i = 0; i < menu->nitems; i++)
	{
		if (EFQ_ItemSelectable((menucommon_s *)menu->items[i]))
		{
			menu->cursor_prev = -1;
			Menu_SetCursor(menu, i);
			break;
		}
	}

	uis.firstdraw = qtrue;
#ifdef _XBOX
	XBLF("STEFX: EF qmenu push menu=%p title='%s' nitems=%d depth=%d cursor=%d",
		(void*)menu,
		menu->title ? menu->title : "",
		menu->nitems,
		uis.menusp,
		menu->cursor);
#endif
}

void UI_PopMenu(void)
{
	if (uis.menusp <= 0)
	{
		uis.activemenu = NULL;
		UI_ForceMenuOff();
		return;
	}

	uis.menusp--;
	if (uis.menusp > 0)
	{
		uis.activemenu = uis.stack[uis.menusp - 1];
		uis.firstdraw = qtrue;
	}
	else
	{
		uis.activemenu = NULL;
		UI_ForceMenuOff();
	}

#ifdef _XBOX
	XBLF("STEFX: EF qmenu pop depth=%d active=%p catcher=0x%x",
		uis.menusp,
		(void*)uis.activemenu,
		ui.Key_GetCatcher());
#endif
}

void Menu_AddItem(menuframework_s *menu, void *item)
{
	menucommon_s *common;

	if (!menu || !item)
	{
		return;
	}

	if (menu->nitems >= MAX_QMENUITEMS)
	{
		ui.Error(ERR_FATAL, "Menu_AddItem: excessive EF qmenu items");
	}

	common = (menucommon_s *)item;
	common->parent = menu;
	common->menuPosition = menu->nitems;
	common->flags &= ~QMF_HASMOUSEFOCUS;

	if (common->type == MTYPE_BITMAP)
	{
		menubitmap_s *bitmap = (menubitmap_s *)item;
		if (!bitmap->shader && common->name && common->name[0])
		{
			bitmap->shader = ui.R_RegisterShaderNoMip(common->name);
		}
		EFQ_SetBounds(common, bitmap->width, bitmap->height);
	}
	else if (common->type == MTYPE_ACTION)
	{
		menuaction_s *action = (menuaction_s *)item;
		EFQ_SetBounds(common, action->width, action->height);
	}
	else if (common->type == MTYPE_TEXT)
	{
		menutext_s *text = (menutext_s *)item;
		int width = text->focusWidth ? text->focusWidth : EFQ_TextWidth(EFQ_ItemText(common), 0.6f);
		int height = text->focusHeight ? text->focusHeight : 18;
		EFQ_SetBounds(common, width, height);
	}
	else if (common->type == MTYPE_SCROLLLIST)
	{
		menulist_s *list = (menulist_s *)item;
		int columns;
		int seperation;
		int width;

		if (list->columns <= 0)
		{
			list->columns = 1;
		}
		if (list->seperation < 0)
		{
			list->seperation = 0;
		}
		list->oldvalue = list->curvalue;
		if (list->curvalue < 0 || list->curvalue >= list->numitems)
		{
			list->curvalue = 0;
		}
		list->top = 0;
		columns = list->columns;
		seperation = list->seperation;
		width = ((list->width + seperation) * columns - seperation) * SMALLCHAR_WIDTH;
		EFQ_SetBounds(common, width, list->height * SMALLCHAR_HEIGHT);
	}

	menu->items[menu->nitems++] = item;
}

void Menu_CursorMoved(menuframework_s *m)
{
	void (*callback)(void *self, int notification);

	if (!m || m->cursor_prev == m->cursor)
	{
		return;
	}

	if (m->cursor_prev >= 0 && m->cursor_prev < m->nitems)
	{
		callback = ((menucommon_s *)m->items[m->cursor_prev])->callback;
		if (callback)
		{
			callback(m->items[m->cursor_prev], QM_LOSTFOCUS);
		}
	}

	if (m->cursor >= 0 && m->cursor < m->nitems)
	{
		callback = ((menucommon_s *)m->items[m->cursor])->callback;
		if (callback)
		{
			callback(m->items[m->cursor], QM_GOTFOCUS);
		}
	}
}

void Menu_SetCursor(menuframework_s *m, int cursor)
{
	if (!m)
	{
		return;
	}

	m->cursor_prev = m->cursor;
	m->cursor = cursor;
	Menu_CursorMoved(m);
}

void Menu_AdjustCursor(menuframework_s *m, int dir)
{
	qboolean wrapped = qfalse;

	if (!m || !m->nitems)
	{
		return;
	}

wrap:
	while (m->cursor >= 0 && m->cursor < m->nitems)
	{
		if (EFQ_ItemSelectable((menucommon_s *)m->items[m->cursor]))
		{
			break;
		}
		m->cursor += dir;
	}

	if (dir > 0 && m->cursor >= m->nitems)
	{
		if (m->wrapAround && !wrapped)
		{
			m->cursor = 0;
			wrapped = qtrue;
			goto wrap;
		}
		m->cursor = m->cursor_prev;
	}
	else if (dir < 0 && m->cursor < 0)
	{
		if (m->wrapAround && !wrapped)
		{
			m->cursor = m->nitems - 1;
			wrapped = qtrue;
			goto wrap;
		}
		m->cursor = m->cursor_prev;
	}
}

void *Menu_ItemAtCursor(menuframework_s *m)
{
	if (!m || m->cursor < 0 || m->cursor >= m->nitems)
	{
		return NULL;
	}

	return m->items[m->cursor];
}

sfxHandle_t Menu_ActivateItem(menuframework_s *s, menucommon_s *item)
{
	(void)s;
	if (item && item->callback)
	{
		item->callback(item, QM_ACTIVATED);
	}
	return 0;
}

void Menu_SetStatusBar(menuframework_s *m, const char *string)
{
	if (m)
	{
		m->statusbar = string;
	}
}

void Menu_Center(menuframework_s *menu)
{
	(void)menu;
}

void Menu_SlideItem(menuframework_s *s, int dir)
{
	(void)s;
	(void)dir;
}

void Menu_Focus(menucommon_s *m)
{
	(void)m;
}

static qboolean EFQ_ScrollListKey(menulist_s *list, int key)
{
	if (!list || list->numitems <= 0)
	{
		return qfalse;
	}

	switch (key)
	{
	case A_CURSOR_UP:
		if (list->curvalue <= 0)
		{
			return qfalse;
		}
		list->oldvalue = list->curvalue;
		list->curvalue--;
		if (list->curvalue < list->top)
		{
			list->top = list->curvalue;
		}
		if (list->generic.callback)
		{
			list->generic.callback(list, QM_GOTFOCUS);
		}
		return qtrue;

	case A_CURSOR_DOWN:
		if (list->curvalue >= list->numitems - 1)
		{
			return qfalse;
		}
		list->oldvalue = list->curvalue;
		list->curvalue++;
		if (list->curvalue >= list->top + list->height)
		{
			list->top = list->curvalue - list->height + 1;
		}
		if (list->generic.callback)
		{
			list->generic.callback(list, QM_GOTFOCUS);
		}
		return qtrue;

	case A_CURSOR_LEFT:
		if (list->curvalue <= 0)
		{
			return qfalse;
		}
		list->oldvalue = list->curvalue;
		list->curvalue -= list->height - 1;
		if (list->curvalue < 0)
		{
			list->curvalue = 0;
		}
		list->top = list->curvalue;
		if (list->generic.callback)
		{
			list->generic.callback(list, QM_GOTFOCUS);
		}
		return qtrue;

	case A_CURSOR_RIGHT:
		if (list->curvalue >= list->numitems - 1)
		{
			return qfalse;
		}
		list->oldvalue = list->curvalue;
		list->curvalue += list->height - 1;
		if (list->curvalue > list->numitems - 1)
		{
			list->curvalue = list->numitems - 1;
		}
		list->top = list->curvalue - list->height + 1;
		if (list->top < 0)
		{
			list->top = 0;
		}
		if (list->generic.callback)
		{
			list->generic.callback(list, QM_GOTFOCUS);
		}
		return qtrue;
	}

	return qfalse;
}

sfxHandle_t Menu_DefaultKey(menuframework_s *m, int key)
{
	menucommon_s *item;
	int cursorPrev;

	if (!m)
	{
		return 0;
	}

	item = (menucommon_s *)Menu_ItemAtCursor(m);
	if (item && item->type == MTYPE_SCROLLLIST && EFQ_ScrollListKey((menulist_s *)item, key))
	{
		return 0;
	}

	switch (key)
	{
	case A_ESCAPE:
	case A_MOUSE2:
		UI_PopMenu();
		return 0;

	case A_CURSOR_UP:
		cursorPrev = m->cursor;
		m->cursor_prev = m->cursor;
		m->cursor--;
		Menu_AdjustCursor(m, -1);
		if (cursorPrev != m->cursor)
		{
			Menu_CursorMoved(m);
		}
		return 0;

	case A_TAB:
	case A_CURSOR_DOWN:
		cursorPrev = m->cursor;
		m->cursor_prev = m->cursor;
		m->cursor++;
		Menu_AdjustCursor(m, 1);
		if (cursorPrev != m->cursor)
		{
			Menu_CursorMoved(m);
		}
		return 0;

	case A_ENTER:
	case A_KP_ENTER:
	case A_MOUSE1:
		if (EFQ_ItemSelectable(item))
		{
			return Menu_ActivateItem(m, item);
		}
		return 0;
	}

	return 0;
}

static void EFQ_DrawBitmap(menubitmap_s *bitmap, qboolean focused)
{
	int color;
	qhandle_t shader;
	const char *text;

	if (!bitmap)
	{
		return;
	}

	color = focused ? bitmap->color2 : bitmap->color;
	if (color <= CT_NONE || color >= CT_MAX)
	{
		color = CT_WHITE;
	}

	shader = bitmap->shader;
	if (!shader && bitmap->generic.name && bitmap->generic.name[0])
	{
		shader = ui.R_RegisterShaderNoMip(bitmap->generic.name);
		bitmap->shader = shader;
	}

	if (shader)
	{
		ui.R_SetColor(colorTable[color]);
		UI_DrawHandlePic((float)bitmap->generic.x, (float)bitmap->generic.y, (float)bitmap->width, (float)bitmap->height, shader);
		ui.R_SetColor(NULL);
	}

	text = EFQ_ItemText(&bitmap->generic);
	if (text && text[0])
	{
		int textColor = focused ? bitmap->textcolor2 : bitmap->textcolor;
		if (textColor <= CT_NONE || textColor >= CT_MAX)
		{
			textColor = focused ? CT_WHITE : CT_BLACK;
		}
		EFQ_DrawText((float)(bitmap->generic.x + bitmap->textX), (float)(bitmap->generic.y + bitmap->textY),
			text, bitmap->textStyle ? bitmap->textStyle : UI_SMALLFONT, textColor);
	}
}

static void EFQ_DrawTextItem(menutext_s *text, qboolean focused)
{
	int color;
	const char *label;

	if (!text)
	{
		return;
	}

	color = focused ? text->color2 : text->color;
	if (color <= CT_NONE || color >= CT_MAX)
	{
		color = focused ? CT_WHITE : CT_LTGOLD1;
	}

	label = EFQ_ItemText(&text->generic);
	EFQ_DrawText((float)text->generic.x, (float)text->generic.y, label, text->style, color);
}

static void EFQ_DrawAction(menuaction_s *action, qboolean focused)
{
	int color;
	int textColor;
	int style;
	const char *label;

	if (!action)
	{
		return;
	}

	color = focused ? action->color2 : action->color;
	textColor = focused ? action->textcolor2 : action->textcolor;
	if (color <= CT_NONE || color >= CT_MAX)
	{
		color = focused ? CT_LTPURPLE1 : CT_DKPURPLE1;
	}
	if (textColor <= CT_NONE || textColor >= CT_MAX)
	{
		textColor = focused ? CT_WHITE : CT_BLACK;
	}

	ui.R_SetColor(colorTable[color]);
	UI_DrawHandlePic((float)action->generic.x, (float)action->generic.y, (float)action->width, (float)action->height, uis.whiteShader);
	ui.R_SetColor(NULL);

	label = EFQ_ItemText(&action->generic);
	style = UI_SMALLFONT;
	EFQ_DrawText((float)(action->generic.x + action->textX), (float)(action->generic.y + action->textY),
		label, style, textColor);
}

static void EFQ_DrawScrollList(menulist_s *list, qboolean focused)
{
	int row;
	int maxRows;
	int textColor;
	int highlightColor;

	if (!list || !list->itemnames || list->numitems <= 0)
	{
		return;
	}

	if (list->top < 0)
	{
		list->top = 0;
	}
	if (list->top > list->numitems - 1)
	{
		list->top = list->numitems - 1;
	}

	maxRows = list->height;
	if (maxRows <= 0)
	{
		maxRows = 1;
	}

	for (row = 0; row < maxRows; row++)
	{
		int itemIndex;
		int y;
		const char *label;

		itemIndex = list->top + row;
		if (itemIndex >= list->numitems)
		{
			break;
		}

		y = list->generic.y + row * SMALLCHAR_HEIGHT;
		label = list->itemnames[itemIndex];
		if (!label)
		{
			label = "";
		}

		if (itemIndex == list->curvalue)
		{
			highlightColor = focused ? CT_LTPURPLE1 : CT_DKPURPLE1;
			textColor = focused ? CT_WHITE : CT_BLACK;
			ui.R_SetColor(colorTable[highlightColor]);
			UI_DrawHandlePic((float)(list->generic.x - 2), (float)y, (float)(list->width * SMALLCHAR_WIDTH), (float)(SMALLCHAR_HEIGHT + 2), uis.whiteShader);
			ui.R_SetColor(NULL);
		}
		else
		{
			textColor = CT_DKGOLD1;
		}

		EFQ_DrawText((float)list->generic.x, (float)(y + 1), label, UI_SMALLFONT, textColor);
	}
}

void Menu_Draw(menuframework_s *menu)
{
	int i;

	if (!menu)
	{
		return;
	}

	if (menu->title && menu->title[0])
	{
		EFQ_DrawText(320.0f, 24.0f, menu->title, UI_BIGFONT | UI_CENTER, CT_LTGOLD1);
	}

	for (i = 0; i < menu->nitems; i++)
	{
		menucommon_s *item = (menucommon_s *)menu->items[i];
		qboolean focused;

		if (!item || (item->flags & QMF_HIDDEN))
		{
			continue;
		}

		if (item->ownerdraw)
		{
			item->ownerdraw(item);
			continue;
		}

		focused = (i == menu->cursor);
		switch (item->type)
		{
		case MTYPE_BITMAP:
			EFQ_DrawBitmap((menubitmap_s *)item, focused);
			break;
		case MTYPE_ACTION:
			EFQ_DrawAction((menuaction_s *)item, focused);
			break;
		case MTYPE_TEXT:
			EFQ_DrawTextItem((menutext_s *)item, focused);
			break;
		case MTYPE_SCROLLLIST:
			EFQ_DrawScrollList((menulist_s *)item, focused);
			break;
		default:
			break;
		}
	}
}

void UI_EFQmenu_Draw(int realtime)
{
	if (!uis.activemenu)
	{
		return;
	}

	uis.frametime = realtime - uis.realtime;
	uis.realtime = realtime;

	if (uis.activemenu->opening)
	{
		uis.activemenu->opening();
	}

	if (uis.activemenu && uis.activemenu->draw)
	{
		uis.activemenu->draw();
	}
	else if (uis.activemenu)
	{
		Menu_Draw(uis.activemenu);
	}
}

void UI_EFQmenu_KeyEvent(int key, qboolean down)
{
	sfxHandle_t sound;

	if (!down || !uis.activemenu)
	{
		return;
	}

	if (uis.activemenu->key)
	{
		sound = uis.activemenu->key(key);
	}
	else
	{
		sound = Menu_DefaultKey(uis.activemenu, key);
	}

	if (sound > 0)
	{
		ui.S_StartLocalSound(sound, CHAN_LOCAL_SOUND);
	}

#ifdef _XBOX
	XBLF("STEFX: EF qmenu key key=%d active=%p depth=%d cursor=%d",
		key,
		(void*)uis.activemenu,
		uis.menusp,
		uis.activemenu ? uis.activemenu->cursor : -1);
#endif
}

static void EFQ_ReturnFromStubMenu(void)
{
#ifdef _XBOX
	XBLF("STEFX: EF qmenu stub return title='%s' main=%d", s_stubTitle, s_stubReturnMain ? 1 : 0);
#endif
	UI_ForceMenuOff();
	if (s_stubReturnMain)
	{
		UI_EFMainMenu_Open();
	}
	else
	{
		UI_EFPauseMenu_Open(NULL);
	}
}

static void EFQ_StubEvent(void *ptr, int notification)
{
	(void)ptr;
	if (notification != QM_ACTIVATED)
	{
		return;
	}
	EFQ_ReturnFromStubMenu();
}

static sfxHandle_t EFQ_StubKey(int key)
{
	switch (key)
	{
	case A_ESCAPE:
	case A_MOUSE2:
		EFQ_ReturnFromStubMenu();
		return 0;
	}

	return Menu_DefaultKey(&s_stubMenu, key);
}

static void EFQ_StubDraw(void)
{
	UI_FillRect(0, 0, 640, 480, colorTable[CT_BLACK]);
	EFQ_DrawText(611, 24, s_stubTitle, UI_BIGFONT | UI_RIGHT, CT_LTGOLD1);
	EFQ_DrawText(320, 198, s_stubLine1, UI_SMALLFONT | UI_CENTER, CT_LTORANGE);
	EFQ_DrawText(320, 232, s_stubLine2, UI_TINYFONT | UI_CENTER, CT_DKGOLD1);
	Menu_Draw(&s_stubMenu);
}

static void EFQ_OpenComingSoonMenu(const char *title, const char *line1, const char *line2)
{
#ifdef _XBOX
	XBLF("STEFX: EF qmenu opening friendly stub title='%s'", title ? title : "");
#endif
	s_stubReturnMain = UI_EFMainMenu_IsActive();
	UI_EFMainMenu_Deactivate();
	UI_EFPauseMenu_Deactivate();
	memset(&s_stubMenu, 0, sizeof(s_stubMenu));
	memset(s_stubButtons, 0, sizeof(s_stubButtons));
	Q_strncpyz(s_stubTitle, title ? title : "ELITE FORCE", sizeof(s_stubTitle));
	Q_strncpyz(s_stubLine1, line1 ? line1 : "COMING SOON", sizeof(s_stubLine1));
	Q_strncpyz(s_stubLine2, line2 ? line2 : "", sizeof(s_stubLine2));

	s_stubMenu.wrapAround = qtrue;
	s_stubMenu.fullscreen = qtrue;
	s_stubMenu.draw = EFQ_StubDraw;
	s_stubMenu.key = EFQ_StubKey;

	EFQ_InitAction(&s_stubButtons[0], EFQ_ID_STUB_RETURN, 255, 318, 130, 18, s_stubReturnMain ? "MAIN MENU" : "INGAME MENU", EFQ_StubEvent);
	Menu_AddItem(&s_stubMenu, &s_stubButtons[0]);
	UI_PushMenu(&s_stubMenu);
}

static void EFQ_KeepMainMenuDumb(const char *cmd, const char *title)
{
#ifdef _XBOX
	XBLF("STEFX: EF qmenu consumed dumb main-menu command cmd='%s' title='%s'", cmd ? cmd : "", title ? title : "");
#else
	(void)cmd;
	(void)title;
#endif
}

qboolean UI_EFQmenu_RouteMenuName(const char *menuName)
{
	if (!menuName || !menuName[0])
	{
		return qfalse;
	}

	if (!Q_stricmp(menuName, "main") || !Q_stricmp(menuName, "mainMenu") || !Q_stricmp(menuName, "splashMenu"))
	{
#ifdef _XBOX
		XBLF("STEFX: EF route parser menu '%s' -> main menu", menuName);
#endif
		UI_EFMainMenu_Open();
		return qtrue;
	}

	if (!Q_stricmp(menuName, "ingameMainMenu"))
	{
		extern void S_StopAllSoundsExceptMusic( void );
#ifdef _XBOX
		XBLF("STEFX: EF route parser menu '%s' -> pause menu", menuName);
#endif
		S_StopAllSoundsExceptMusic();
		UI_EFPauseMenu_Open(menuName);
		return qtrue;
	}

	if (!Q_stricmp(menuName, "newgame") || !Q_stricmp(menuName, "characterMenu") ||
		!Q_stricmp(menuName, "ingameMissionSelect1") || !Q_stricmp(menuName, "ingameMissionSelect2") ||
		!Q_stricmp(menuName, "ingameMissionSelect3"))
	{
		return UI_EFQmenu_ConsoleCommand("ui_ef_newgame");
	}
	if (!Q_stricmp(menuName, "loadgame") || !Q_stricmp(menuName, "loadMenu") ||
		!Q_stricmp(menuName, "loadgameMenu") || !Q_stricmp(menuName, "ingameloadMenu") ||
		!Q_stricmp(menuName, "missionfailed_menu"))
	{
		return UI_EFQmenu_ConsoleCommand("ui_ef_loadgame");
	}
	if (!Q_stricmp(menuName, "savegame") || !Q_stricmp(menuName, "saveMenu") ||
		!Q_stricmp(menuName, "savegameMenu") || !Q_stricmp(menuName, "ingamesaveMenu"))
	{
		return UI_EFQmenu_ConsoleCommand("ui_ef_savegame");
	}
	if (!Q_stricmp(menuName, "configure") || !Q_stricmp(menuName, "controlsMenu") ||
		!Q_stricmp(menuName, "setupMenu") || !Q_stricmp(menuName, "ingamecontrolsMenu") ||
		!Q_stricmp(menuName, "ingamesetupMenu"))
	{
		return UI_EFQmenu_ConsoleCommand("ui_ef_configure");
	}
	if (!Q_stricmp(menuName, "holomatch") || !Q_stricmp(menuName, "holomatchMenu"))
	{
		return UI_EFQmenu_ConsoleCommand("ui_ef_holomatch");
	}
	if (!Q_stricmp(menuName, "crew") || !Q_stricmp(menuName, "crewMenu") || !Q_stricmp(menuName, "voyagerCrew"))
	{
		return UI_EFQmenu_ConsoleCommand("ui_ef_crew");
	}
	if (!Q_stricmp(menuName, "credits") || !Q_stricmp(menuName, "creditsMenu"))
	{
		return UI_EFQmenu_ConsoleCommand("ui_ef_credits");
	}
	if (!Q_stricmp(menuName, "tour") || !Q_stricmp(menuName, "tourMenu") || !Q_stricmp(menuName, "virtualVoyager"))
	{
		return UI_EFQmenu_ConsoleCommand("ui_ef_tour");
	}
	if (!Q_stricmp(menuName, "mods") || !Q_stricmp(menuName, "modsMenu"))
	{
		return UI_EFQmenu_ConsoleCommand("ui_ef_mods");
	}
	if (!Q_stricmp(menuName, "leavegame") || !Q_stricmp(menuName, "ingamequitMenu"))
	{
		return UI_EFQmenu_ConsoleCommand("ui_ef_leavegame");
	}
	if (!Q_stricmp(menuName, "quitgame") || !Q_stricmp(menuName, "quitMenu"))
	{
		return UI_EFQmenu_ConsoleCommand("ui_ef_quit");
	}

	return qfalse;
}

qboolean UI_EFQmenu_ConsoleCommand(const char *cmd)
{
	// Route EF-owned menu commands away from inherited JA parser screens.
	if (!cmd || !cmd[0])
	{
		return qfalse;
	}

	if (!Q_stricmp(cmd, "ui_ef_newgame"))
	{
		UI_EFMainMenu_OpenNewGame();
		return qtrue;
	}
	if (!Q_stricmp(cmd, "ui_ef_loadgame"))
	{
		UI_EFMainMenu_OpenLoadGame();
		return qtrue;
	}
	if (!Q_stricmp(cmd, "ui_ef_savegame"))
	{
		EFQ_KeepMainMenuDumb(cmd, "SAVE GAME");
		return qtrue;
	}
	if (!Q_stricmp(cmd, "ui_ef_configure"))
	{
		UI_EFMainMenu_OpenConfigure();
		return qtrue;
	}
	if (!Q_stricmp(cmd, "ui_ef_audio"))
	{
		UI_EFMainMenu_OpenAudio();
		return qtrue;
	}
	if (!Q_stricmp(cmd, "ui_ef_video"))
	{
		UI_EFMainMenu_OpenVideo();
		return qtrue;
	}
	if (!Q_stricmp(cmd, "ui_ef_controller"))
	{
		UI_EFMainMenu_OpenController();
		return qtrue;
	}
	if (!Q_stricmp(cmd, "ui_ef_holomatch"))
	{
		UI_EFMainMenu_StartSplitScreenBaseline();
		return qtrue;
	}
	if (!Q_stricmp(cmd, "ui_ef_crew"))
	{
		UI_EFMainMenu_OpenStub("ELITE FORCE : VOYAGER CREW", "THIS MENU IS NOT AVAILABLE YET");
		return qtrue;
	}
	if (!Q_stricmp(cmd, "ui_ef_credits"))
	{
		EFQ_KeepMainMenuDumb(cmd, "CREDITS");
		return qtrue;
	}
	if (!Q_stricmp(cmd, "ui_ef_tour"))
	{
		EFQ_KeepMainMenuDumb(cmd, "VIRTUAL VOYAGER");
		return qtrue;
	}
	if (!Q_stricmp(cmd, "ui_ef_mods"))
	{
		EFQ_KeepMainMenuDumb(cmd, "MODS");
		return qtrue;
	}
	if (!Q_stricmp(cmd, "ui_ef_quit"))
	{
		EFQ_KeepMainMenuDumb(cmd, "QUIT");
		return qtrue;
	}
	if (!Q_stricmp(cmd, "ui_ef_leavegame"))
	{
		EFQ_KeepMainMenuDumb(cmd, "LEAVE GAME");
		return qtrue;
	}

	return qfalse;
}
