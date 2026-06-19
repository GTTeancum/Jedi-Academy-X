#ifndef __CG_TEXT_PRECACHE_H__
#define __CG_TEXT_PRECACHE_H__

#define MAX_PRECACHEWAV  256
#define MAX_PRECACHETEXT 512

typedef struct precacheWav_s
{
	char *wavFile;
	char textKey[8];
	int speaker;
} precacheWav_t;

typedef struct precacheText_s
{
	char *key;
	char *text;
} precacheText_t;

extern int precacheWav_i;
extern precacheWav_t precacheWav[MAX_PRECACHEWAV];

extern int precacheText_i;
extern precacheText_t precacheText[MAX_PRECACHETEXT];

int CG_SearchTextPrecache( char *key );
int CG_SearchWavPrecache( char *key );

#endif
