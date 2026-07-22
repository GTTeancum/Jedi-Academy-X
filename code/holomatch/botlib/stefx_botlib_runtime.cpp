#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "../official/game/q_shared.h"

vec3_t vec3_origin = { 0.0f, 0.0f, 0.0f };

void Q_strncpyz(char *dest, const char *src, int destsize)
{
	if (!dest || !src || destsize < 1)
	{
		return;
	}

	strncpy(dest, src, destsize - 1);
	dest[destsize - 1] = '\0';
}

char *QDECL va(char *format, ...)
{
	va_list argptr;
	static char strings[2][32000];
	static int index;
	char *buffer = strings[index++ & 1];

	va_start(argptr, format);
	vsprintf(buffer, format, argptr);
	va_end(argptr);

	return buffer;
}
