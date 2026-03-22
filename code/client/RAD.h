// RAD.h stub for Xbox build - Rad Game Tools SDK not available
#ifndef RAD_H
#define RAD_H

typedef unsigned int U32;
typedef int S32;

#define PTR4
#define RADEXPLINK __cdecl

typedef void* (RADEXPLINK *RADMemAlloc)(U32 size);
typedef void  (RADEXPLINK *RADMemFree)(void PTR4* ptr);

inline void RADSetMemory(RADMemAlloc a, RADMemFree f) {}

#endif // RAD_H
