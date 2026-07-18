// Elite Force Holomatch keeps the shared animation-name table in gameplay code.
// The inherited Xbox MP build used the UI library as the one-copy owner, but
// efmp's UI library is intentionally reduced to an inert menu stub.

#include "q_shared.h"
#include "anims.h"

#define STEFX_ANIMTABLE_DEFINE
#include "../cgame/animtable.h"
