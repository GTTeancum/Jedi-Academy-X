// bink.h stub for Xbox build - Rad Game Tools Bink SDK not available
#ifndef BINK_H
#define BINK_H

#include <stddef.h>

#define BINKALPHA       0x00800000
#define BINKSNDTRACK    0x00002000
#define BINKCOPYALL     0x00000080
#define BINKSURFACE32A  0x00000008
#define BINKSURFACEYUY2 0x00000040

struct BINK
{
    unsigned int Width;
    unsigned int Height;
    unsigned int Frames;
    unsigned int FrameNum;
    unsigned int OpenFlags;
};
typedef struct BINK *HBINK;

inline HBINK   BinkOpen(const char*, unsigned int)       { return NULL; }
inline void    BinkClose(HBINK)                          {}
inline int     BinkWait(HBINK)                           { return 0; }
inline void    BinkDoFrame(HBINK)                        {}
inline void    BinkNextFrame(HBINK)                      {}
inline int     BinkCopyToBuffer(HBINK, void*, int, int, int, int, unsigned int) { return 0; }
inline void    BinkSetVolume(HBINK, int, int)            {}
inline void    BinkSetSoundTrack(int, unsigned int*)     {}
inline void    BinkSetMixBins(HBINK, int, unsigned int*, int) {}

#endif // BINK_H
