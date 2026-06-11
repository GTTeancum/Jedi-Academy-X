#include "g_local.h"
#include "../../code/win32/xb_log.h"

short cg_crossHairStatus = 0;
int g_lastFireTime = 0;
qboolean MatrixMode = qfalse;
int cg_saberOnSoundTime[MAX_GENTITIES] = {0};
char current_speeders = 0;
bool dontPillarPush = false;
char cinematicSkipScript[64] = {0};
vec3_t vec3_origin = {0, 0, 0};
vec3_t axisDefault[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

#define STEFX_SABER_PARMS_SIZE 0x40000
char SaberParms[STEFX_SABER_PARMS_SIZE];

static char stefx_com_token[MAX_TOKEN_CHARS];
static int stefx_com_lines;

void COM_BeginParseSession( void )
{
	stefx_com_lines = 0;
}

int Q_strncmp( const char *s1, const char *s2, int n )
{
	int c1, c2;

	do
	{
		c1 = *s1++;
		c2 = *s2++;
		if ( !n-- )
		{
			return 0;
		}
		if ( c1 != c2 )
		{
			return -1;
		}
	} while ( c1 );

	return 0;
}

static char *STEFX_SkipWhitespace( char *data, qboolean *hasNewLines )
{
	int c;

	while ( ( c = *data ) <= ' ' )
	{
		if ( !c )
		{
			return NULL;
		}
		if ( c == '\n' )
		{
			stefx_com_lines++;
			*hasNewLines = qtrue;
		}
		data++;
	}

	return data;
}

char *COM_Parse( char **data_p )
{
	return COM_ParseExt( data_p, qtrue );
}

char *COM_ParseExt( char **data_p, qboolean allowLineBreaks )
{
	int c = 0;
	int len = 0;
	qboolean hasNewLines = qfalse;
	char *data = *data_p;

	stefx_com_token[0] = 0;
	if ( !data )
	{
		*data_p = NULL;
		return stefx_com_token;
	}

	while ( 1 )
	{
		data = STEFX_SkipWhitespace( data, &hasNewLines );
		if ( !data )
		{
			*data_p = NULL;
			return stefx_com_token;
		}
		if ( hasNewLines && !allowLineBreaks )
		{
			*data_p = data;
			return stefx_com_token;
		}

		c = *data;
		if ( c == '/' && data[1] == '/' )
		{
			while ( *data && *data != '\n' )
			{
				data++;
			}
		}
		else if ( c == '/' && data[1] == '*' )
		{
			while ( *data && ( *data != '*' || data[1] != '/' ) )
			{
				data++;
			}
			if ( *data )
			{
				data += 2;
			}
		}
		else
		{
			break;
		}
	}

	if ( c == '\"' )
	{
		data++;
		while ( 1 )
		{
			c = *data++;
			if ( c == '\"' || !c )
			{
				stefx_com_token[len] = 0;
				*data_p = data;
				return stefx_com_token;
			}
			if ( len < MAX_TOKEN_CHARS )
			{
				stefx_com_token[len++] = c;
			}
		}
	}

	do
	{
		if ( len < MAX_TOKEN_CHARS )
		{
			stefx_com_token[len++] = c;
		}
		data++;
		c = *data;
		if ( c == '\n' )
		{
			stefx_com_lines++;
		}
	} while ( c > 32 );

	if ( len == MAX_TOKEN_CHARS )
	{
		len = 0;
	}
	stefx_com_token[len] = 0;
	*data_p = data;
	return stefx_com_token;
}

void SkipBracedSection( char **program )
{
	char *token;
	int depth = 0;

	if ( stefx_com_token[0] == '{' )
	{
		depth = 1;
	}
	do
	{
		token = COM_ParseExt( program, qtrue );
		if ( token[1] == 0 )
		{
			if ( token[0] == '{' )
			{
				depth++;
			}
			else if ( token[0] == '}' )
			{
				depth--;
			}
		}
	} while ( depth && *program );
}

void SkipRestOfLine( char **data )
{
	char *p = *data;
	int c;

	while ( ( c = *p++ ) != 0 )
	{
		if ( c == '\n' )
		{
			stefx_com_lines++;
			break;
		}
	}

	*data = p;
}

char *QDECL va( char *format, ... )
{
	va_list argptr;
	static char string[2][32000];
	static int index = 0;
	char *buf = string[index & 1];
	index++;

	va_start( argptr, format );
	vsprintf( buf, format, argptr );
	va_end( argptr );

	return buf;
}

int GetIDForString( stringID_table_t *table, const char *string )
{
	int index = 0;

	while ( table[index].name && table[index].name[0] )
	{
		if ( !Q_stricmp( table[index].name, string ) )
		{
			return table[index].id;
		}
		index++;
	}

	return -1;
}

const char *GetStringForID( stringID_table_t *table, int id )
{
	int index = 0;

	while ( table[index].name && table[index].name[0] )
	{
		if ( table[index].id == id )
		{
			return table[index].name;
		}
		index++;
	}

	return NULL;
}

float LerpAngle( float from, float to, float frac )
{
	if ( to - from > 180 )
	{
		to -= 360;
	}
	if ( to - from < -180 )
	{
		to += 360;
	}
	return from + frac * ( to - from );
}

float AngleSubtract( float a1, float a2 )
{
	float a = a1 - a2;
	while ( a > 180 )
	{
		a -= 360;
	}
	while ( a < -180 )
	{
		a += 360;
	}
	return a;
}

void AnglesSubtract( vec3_t v1, vec3_t v2, vec3_t v3 )
{
	v3[0] = AngleSubtract( v1[0], v2[0] );
	v3[1] = AngleSubtract( v1[1], v2[1] );
	v3[2] = AngleSubtract( v1[2], v2[2] );
}

float AngleMod( float a )
{
	return ( 360.0f / 65536.0f ) * ( (int)( a * ( 65536.0f / 360.0f ) ) & 65535 );
}

float AngleDelta( float angle1, float angle2 )
{
	return AngleNormalize180( angle1 - angle2 );
}

void AxisCopy( vec3_t in[3], vec3_t out[3] )
{
	VectorCopy( in[0], out[0] );
	VectorCopy( in[1], out[1] );
	VectorCopy( in[2], out[2] );
}

float Vector4to3( const vec4_t in, vec3_t out )
{
	out[0] = in[0];
	out[1] = in[1];
	out[2] = in[2];
	return in[3];
}

void GetAnglesForDirection( const vec3_t p1, const vec3_t p2, vec3_t out )
{
	vec3_t v;
	VectorSubtract( p2, p1, v );
	vectoangles( v, out );
}

void G_EntityPosition( int i, vec3_t ret )
{
	if ( !ret )
	{
		return;
	}

	if ( !g_entities || i < 0 || i >= globals.num_entities )
	{
		VectorClear( ret );
		return;
	}

	VectorCopy( g_entities[i].currentOrigin, ret );
}

void G_AllocGentities( void )
{
	XBLog_Write("STEFX: G_AllocGentities deferred to Elite Force InitGame");
}

void ClearHStringPool( void )
{
}

void BG_ClearVehicles( void )
{
}

void ClearAllNavStructures( void )
{
}

void ClearModelsAlreadyDone( void )
{
}

void G_ASPreCacheFree( void )
{
}

void G_FreeRoffs( void )
{
}

bool Cheat_InfiniteForce( void )
{
	XBLog_Write("STEFX: ignored Jedi Academy force cheat");
	return false;
}

bool Cheat_ChangeSaber( void )
{
	XBLog_Write("STEFX: ignored Jedi Academy saber cheat");
	return false;
}

void CG_SetDataPadWeaponText( void )
{
}

void CG_SetDataPadForceText( void )
{
}

void CG_CenterPrint( const char *str, int y )
{
	XBLog_Writef("STEFX: early two-arg CG_CenterPrint y=%d text=%s", y, str ? str : "");
}
