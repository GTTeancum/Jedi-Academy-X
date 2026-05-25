#ifndef RAD_H
#define RAD_H

#include "bink.h"

inline void RADSetMemory(BINKMEMALLOC a, BINKMEMFREE f)
{
	BinkSetMemory(a, f);
}

#endif // RAD_H
