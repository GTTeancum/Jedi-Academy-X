// client_ui.h -- header for client access to ui funcs
#ifndef __CLIENTUI_H__
#define __CLIENTUI_H__

#include "../ui/ui_public.h"

void _UI_KeyEvent( int key, qboolean down );
void UI_SetActiveMenu( const char* menuname,const char *menuID );
void UI_UpdateConnectionMessageString( char *string );
qboolean UI_ConsoleCommand( void ) ;
qboolean _UI_IsFullscreen( void );
#if defined(_XBOX) && defined(STEFX_ELITE_FORCE_SP)
qboolean UI_STEFX_ShouldDispatchGameplayMenuBind( void );
#endif

#endif //__CLIENTUI_H__
