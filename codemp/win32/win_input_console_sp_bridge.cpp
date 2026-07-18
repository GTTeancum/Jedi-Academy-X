// Compile the wholesale SP console-input implementation against MP client state.
#include "../server/exe_headers.h"
#include "../client/client.h"

typedef struct {
	playerState_t ps;
} stefxInputClient_t;

typedef struct {
	stefxInputClient_t *client;
} stefxInputEntity_t;

static stefxInputEntity_t *STEFX_SPInputEntities( void )
{
	static stefxInputClient_t inputClient;
	static stefxInputEntity_t inputEntity = { &inputClient };

	if ( cl ) {
		inputClient.ps.legsAnim = cl->snap.ps.legsAnim;
	}
	return &inputEntity;
}

bool in_camera = false;

static qboolean STEFX_MPGameAllowedToSaveHere( qboolean )
{
	return qtrue;
}

#define g_entities STEFX_SPInputEntities()
#define SG_GameAllowedToSaveHere STEFX_MPGameAllowedToSaveHere
#include "win_input_console.cpp"
#undef SG_GameAllowedToSaveHere
#undef g_entities
