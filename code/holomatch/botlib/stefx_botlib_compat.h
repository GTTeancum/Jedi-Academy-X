#ifndef STEFX_HOLOMATCH_BOTLIB_COMPAT_H
#define STEFX_HOLOMATCH_BOTLIB_COMPAT_H

#include <string.h>

#ifndef MAX_TOKENLENGTH
#define MAX_TOKENLENGTH 1024
#endif

typedef struct pc_token_s
{
	int type;
	int subtype;
	int intvalue;
	float floatvalue;
	char string[MAX_TOKENLENGTH];
} pc_token_t;

#define Com_Memcpy memcpy
#define Com_Memset memset

#ifndef Square
#define Square(x) ((x) * (x))
#endif

int COM_Compress(char *data_p);

#endif
