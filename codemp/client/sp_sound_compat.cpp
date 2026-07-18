// Preserve MP's registration ABI while SP owns the sound implementation.
#include "../server/exe_headers.h"
#include "client.h"

qboolean s_shutUp = qfalse;
cvar_t *g_sex = NULL;

char *FS_BuildOSPathUnMapped( const char *qpath )
{
	static char ospath[2][MAX_OSPATH];
	static int toggle;
	char *out;

	toggle ^= 1;
	out = ospath[toggle];

	while ( *qpath == '/' || *qpath == '\\' )
	{
		++qpath;
	}

	Com_sprintf( out, sizeof( ospath[0] ), "d:\\BaseEF\\%s", qpath );
	for ( char *cursor = out; *cursor; ++cursor )
	{
		if ( *cursor == '/' )
		{
			*cursor = '\\';
		}
	}

	return out;
}

void S_BeginRegistration( int )
{
	if ( !g_sex )
	{
		g_sex = Cvar_Get( "sex", "f", CVAR_USERINFO | CVAR_ARCHIVE );
	}

	S_BeginRegistration();
}
