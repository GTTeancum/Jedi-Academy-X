#include "../server/exe_headers.h"
#include "../game/anims.h"

// The SP UI remains part of the executable, so retain its exact animation
// lookup table even though the official EF cgame owns live player animation.
#define STEFX_DEFINE_ANIMTABLE
#include "../cgame/animtable.h"

// The SP connection UI reads this while drawing loading screens. Holomatch
// never enters the savegame path, so its state remains a normal fresh load.
SavedGameJustLoaded_e g_eSavedGameJustLoaded = eNO;

// The SP renderer reads the campaign cgame's cinematic-camera state.  The
// Holomatch cgame has no campaign camera, but keeps the same engine contract.
bool in_camera = false;

// In SP this storage normally comes from the campaign game module.  Keep the
// same UI-owned buffer available when that module is replaced by Holomatch.
char SaberParms[0x8000];
