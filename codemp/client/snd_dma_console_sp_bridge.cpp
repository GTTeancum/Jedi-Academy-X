#include "../server/exe_headers.h"
#include "snd_local_console.h"
#include "snd_music.h"
#include "client.h"

static int STEFX_SoundZFree( void *ptr )
{
	if ( !ptr )
	{
		return 0;
	}

	const int bytes = Z_Size( ptr );
	Z_Free( ptr );
	return bytes;
}

#define Z_Free STEFX_SoundZFree
#include "snd_dma_console.cpp"
#undef Z_Free
