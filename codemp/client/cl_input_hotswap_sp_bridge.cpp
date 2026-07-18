// Compile the wholesale SP hot-swap implementation against MP's active cgame.
#include "../qcommon/exe_headers.h"
#include "client.h"
#include "../cgame/cg_local.h"

#define INV_MAX 7
#define MAX_SHOWPOWERS 12
#define cg (*cg)
#define forcepowerSelect forceSelect
#define forcepowerSelectTime forceSelectTime
#define inventorySelect itemSelect
#define inventorySelectTime invenSelectTime
#define cgi_S_RegisterSound S_RegisterSound
#define cgi_S_StartLocalSound S_StartLocalSound
#include "cl_input_hotswap.cpp"
#undef cgi_S_StartLocalSound
#undef cgi_S_RegisterSound
#undef inventorySelectTime
#undef inventorySelect
#undef forcepowerSelectTime
#undef forcepowerSelect
#undef cg
#undef MAX_SHOWPOWERS
#undef INV_MAX
