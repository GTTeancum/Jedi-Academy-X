// Compile the wholesale SP rumble implementation against MP's active cgame.
#include "../server/exe_headers.h"
#include "../cgame/cg_local.h"

#define cg (*cg)
#include "win_input_rumble.cpp"
#undef cg
