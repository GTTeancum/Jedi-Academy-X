// SP input owns behavior; this unit supplies the MP executable service boundaries.
#include "../server/exe_headers.h"
#include "client.h"
#include "client_ui.h"

void _UI_MouseEvent( int dx, int dy )
{
	if ( uivm ) {
		VM_Call( uivm, UI_MOUSE_EVENT, dx, dy );
	}
}

void _UI_KeyEvent( int key, qboolean down )
{
	if ( uivm ) {
		VM_Call( uivm, UI_KEY_EVENT, key, down );
	}
}

qboolean _UI_IsFullscreen( void )
{
	if ( !uivm ) {
		return qfalse;
	}

	return (qboolean)VM_Call( uivm, UI_IS_FULLSCREEN );
}

void UI_SetActiveMenu( const char *menuname, const char *menuID )
{
	int menu = UIMENU_MAIN;

	if ( !uivm ) {
		return;
	}

	if ( !menuname || !menuname[0] ) {
		menu = UIMENU_NONE;
	} else if ( !Q_stricmp( menuname, "ingame" ) ) {
		menu = ( menuID && !Q_stricmp( menuID, "noController" ) )
			? UIMENU_NOCONTROLLERINGAME : UIMENU_INGAME;
	} else if ( !Q_stricmp( menuname, "ui_popup" ) &&
		menuID && !Q_stricmp( menuID, "noController" ) ) {
		menu = UIMENU_NOCONTROLLER;
	}

	VM_Call( uivm, UI_SET_ACTIVE_MENU, menu );
}

bool Cheat_InfiniteForce( void )
{
	return false;
}

bool Cheat_ChangeSaber( void )
{
	return false;
}

bool UI_ForceConfigUIActive( void )
{
	return false;
}
