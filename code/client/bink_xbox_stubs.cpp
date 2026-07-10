#include "bink.h"
#include "RAD.h"

extern "C" {

void RADEXPLINK RADSetMemory(BINKMEMALLOC, BINKMEMFREE)
{
}

HBINK RADEXPLINK BinkOpen(const char PTR4*, U32)
{
	return 0;
}

S32 RADEXPLINK BinkDoFrame(HBINK)
{
	return 0;
}

void RADEXPLINK BinkNextFrame(HBINK)
{
}

S32 RADEXPLINK BinkWait(HBINK)
{
	return 0;
}

void RADEXPLINK BinkClose(HBINK)
{
}

S32 RADEXPLINK BinkCopyToBuffer(HBINK, void*, S32, U32, U32, U32, U32)
{
	return 0;
}

void RADEXPLINK BinkSetVolume(HBINK, S32)
{
}

}
