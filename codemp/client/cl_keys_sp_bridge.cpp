// Compile the wholesale SP key system while retaining MP-owned service boundaries.
#include "../server/exe_headers.h"
#include "client.h"

static const char *s_completionPartial;
static const char *s_completionLast;
static const char *s_completionMatch;
static qboolean s_completionAfterLast;
static qboolean s_completionExact;

static void STEFX_CompletionCallback( const char *name )
{
	int len;

	if ( s_completionLast && !s_completionAfterLast ) {
		if ( !Q_stricmp( name, s_completionLast ) ) {
			s_completionAfterLast = qtrue;
		}
		return;
	}

	len = strlen( s_completionPartial );
	if ( Q_stricmpn( s_completionPartial, name, len ) ) {
		return;
	}

	if ( !Q_stricmp( s_completionPartial, name ) ) {
		s_completionMatch = name;
		s_completionExact = qtrue;
	} else if ( !s_completionMatch ) {
		s_completionMatch = name;
	}
}

static char *STEFX_CompleteFromRegistry( const char *partial, const char *last, qboolean commands )
{
	if ( !partial || !partial[0] ) {
		return NULL;
	}

	s_completionPartial = partial;
	s_completionLast = last;
	s_completionMatch = NULL;
	s_completionAfterLast = (qboolean)( last == NULL );
	s_completionExact = qfalse;

	if ( commands ) {
		Cmd_CommandCompletion( STEFX_CompletionCallback );
	} else {
		Cvar_CommandCompletion( STEFX_CompletionCallback );
	}

	return (char *)s_completionMatch;
}

char *Cmd_CompleteCommand( const char *partial )
{
	return STEFX_CompleteFromRegistry( partial, NULL, qtrue );
}

char *Cmd_CompleteCommandNext( char *partial, char *last )
{
	return STEFX_CompleteFromRegistry( partial, last, qtrue );
}

char *Cvar_CompleteVariable( const char *partial )
{
	return STEFX_CompleteFromRegistry( partial, NULL, qfalse );
}

char *Cvar_CompleteVariableNext( char *partial, char *last )
{
	return STEFX_CompleteFromRegistry( partial, last, qfalse );
}

static void STEFX_SPKeysDrawSmallChar( int, int, int )
{
}

static void STEFX_SPKeysDrawBigString( int, int, const char *, float )
{
}

static void STEFX_SPKeysStopCinematic( qboolean )
{
	SCR_StopCinematic();
}

#define SCR_DrawSmallChar STEFX_SPKeysDrawSmallChar
#define SCR_DrawBigString STEFX_SPKeysDrawBigString
#define SCR_StopCinematic STEFX_SPKeysStopCinematic
#include "cl_keys.cpp"
#undef SCR_StopCinematic
#undef SCR_DrawBigString
#undef SCR_DrawSmallChar

int Key_GetKey( const char *binding )
{
	int i;

	if ( binding ) {
		for ( i = 0 ; i < MAX_KEYS ; i++ ) {
			if ( kg.keys[i].binding && !Q_stricmp( binding, kg.keys[i].binding ) ) {
				return i;
			}
		}
	}

	return -1;
}
