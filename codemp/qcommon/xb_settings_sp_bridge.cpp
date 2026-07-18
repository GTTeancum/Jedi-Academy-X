// Keep the SP settings implementation intact while routing its media asset through BaseEF.
#include <xtl.h>

#undef CopyFile
#define CopyFile(source, destination, failIfExists) \
	CopyFileA( "D:\\BaseEF\\media\\settings.xbx", destination, failIfExists )
#include "xb_settings.cpp"
#undef CopyFile
