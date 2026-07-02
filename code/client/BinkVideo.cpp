/*
 * This version of BinkVideo.cpp now ONLY works on Xbox.
 * GCN support is hosed.
 */
#include "snd_local_console.h"
#include "../renderer/tr_local.h"
#include "BinkVideo.h"
#include "RAD.h"
#include "../win32/xbox_texture_man.h"
#include "../win32/xb_log.h"
#include <xgraphics.h>

#include "../client/client.h"

char *binkSndMem = NULL;

// Taste the hackery!
extern void *BonePoolTempAlloc( unsigned long size );
extern void BonePoolTempFree( void *p );
extern void *TempAlloc( unsigned long size );
extern void TempFree( void );

extern void SP_DrawSPLoadScreen( void );

#ifdef _XBOX
extern "C" volatile unsigned int g_SPXBCinPhase;
extern "C" volatile unsigned int g_SPXBCinStatus;
extern "C" volatile unsigned int g_SPXBCinLoopCount;
extern "C" volatile unsigned int g_SPXBBinkPhase;
extern "C" volatile unsigned int g_SPXBBinkRunCount;
extern "C" volatile unsigned int g_SPXBBinkWaitLoops;
extern "C" volatile unsigned int g_SPXBBinkWaitBreaks;
extern "C" volatile unsigned int g_SPXBBinkFrameNum;
extern "C" volatile unsigned int g_SPXBBinkFrames;
extern "C" volatile unsigned int g_SPXBBinkOpenFlags;
extern "C" volatile unsigned int g_SPXBBinkWidth;
extern "C" volatile unsigned int g_SPXBBinkHeight;
extern "C" volatile unsigned int g_SPXBBinkAlpha;
extern "C" volatile unsigned int g_SPXBBinkCopySkipped;
extern "C" volatile unsigned int g_SPXBBinkStartResult;
extern "C" volatile unsigned int g_SPXBBinkStatus;
extern "C" volatile unsigned int g_SPXBBinkAllocSeq;
extern "C" volatile unsigned int g_SPXBBinkLastAllocSize;
extern "C" volatile unsigned int g_SPXBBinkLastAllocPtr;
extern "C" volatile unsigned int g_SPXBBinkFreeSeq;
extern "C" volatile unsigned int g_SPXBBinkLastFreePtr;
extern "C" volatile unsigned int g_SPXBBinkLastAvailPhys;
extern "C" volatile unsigned int g_SPXBBinkLastZoneAlloc;
extern "C" volatile unsigned int g_SPXBBinkLastZoneFree;
extern "C" volatile unsigned int g_SPXBBinkLastTempPool;
extern "C" volatile unsigned int g_SPXBBinkMemCode;
extern "C" volatile unsigned int g_SPXBBinkOutstandingCount;
extern "C" volatile unsigned int g_SPXBBinkOutstandingBytes;
extern "C" volatile unsigned int g_SPXBBinkPeakOutstandingBytes;
#endif

#ifndef SP_XBOX_BINK_WAIT_CAP
#define SP_XBOX_BINK_WAIT_CAP 2500000
#endif

#ifdef _XBOX
static void BinkLogMemoryState(unsigned int code, const char *where)
{
	MEMORYSTATUS stat;
	xboxZoneStats_t zoneStats;

	memset(&stat, 0, sizeof(stat));
	memset(&zoneStats, 0, sizeof(zoneStats));
	GlobalMemoryStatus(&stat);
	Z_XboxGetStats(&zoneStats);
	g_SPXBBinkMemCode = code;
	g_SPXBBinkLastAvailPhys = (unsigned int)stat.dwAvailPhys;
	g_SPXBBinkLastZoneAlloc = (unsigned int)zoneStats.sizeAlloc;
	g_SPXBBinkLastZoneFree = (unsigned int)zoneStats.sizeFree;
	g_SPXBBinkLastTempPool = (unsigned int)zoneStats.tempPoolUsed;

	XBLF("JA: BinkVideo::Mem %s availPhys=%u zoneAlloc=%d zoneFree=%d peak=%d level=%d hunk=%d tempHunk=%d misc=%d sound=%d bink=%d tempPool=%d",
		where ? where : "<null>",
		(unsigned int)stat.dwAvailPhys,
		zoneStats.sizeAlloc,
		zoneStats.sizeFree,
		zoneStats.peakAlloc,
		zoneStats.levelMemory,
		zoneStats.hunkMemory,
		zoneStats.tempHunkMemory,
		zoneStats.miscMemory,
		zoneStats.soundMemory,
		zoneStats.binkMemory,
		zoneStats.tempPoolUsed);
}

typedef struct binkAllocTrack_s
{
	void *ptr;
	U32 size;
} binkAllocTrack_t;

static binkAllocTrack_t s_binkAllocTrack[32];

static void BinkTrackAlloc(void *ptr, U32 size)
{
	int i;

	if( !ptr )
	{
		return;
	}

	for( i = 0; i < (int)(sizeof(s_binkAllocTrack) / sizeof(s_binkAllocTrack[0])); ++i )
	{
		if( s_binkAllocTrack[i].ptr == ptr )
		{
			if( g_SPXBBinkOutstandingBytes >= s_binkAllocTrack[i].size )
			{
				g_SPXBBinkOutstandingBytes -= s_binkAllocTrack[i].size;
			}
			s_binkAllocTrack[i].size = size;
			g_SPXBBinkOutstandingBytes += size;
			if( g_SPXBBinkOutstandingBytes > g_SPXBBinkPeakOutstandingBytes )
			{
				g_SPXBBinkPeakOutstandingBytes = g_SPXBBinkOutstandingBytes;
			}
			return;
		}
	}

	for( i = 0; i < (int)(sizeof(s_binkAllocTrack) / sizeof(s_binkAllocTrack[0])); ++i )
	{
		if( !s_binkAllocTrack[i].ptr )
		{
			s_binkAllocTrack[i].ptr = ptr;
			s_binkAllocTrack[i].size = size;
			++g_SPXBBinkOutstandingCount;
			g_SPXBBinkOutstandingBytes += size;
			if( g_SPXBBinkOutstandingBytes > g_SPXBBinkPeakOutstandingBytes )
			{
				g_SPXBBinkPeakOutstandingBytes = g_SPXBBinkOutstandingBytes;
			}
			return;
		}
	}

	XBLog_Write("JA: BinkVideo::TrackAlloc overflow");
}

static void BinkTrackFree(void *ptr)
{
	int i;

	if( !ptr )
	{
		return;
	}

	for( i = 0; i < (int)(sizeof(s_binkAllocTrack) / sizeof(s_binkAllocTrack[0])); ++i )
	{
		if( s_binkAllocTrack[i].ptr == ptr )
		{
			if( g_SPXBBinkOutstandingBytes >= s_binkAllocTrack[i].size )
			{
				g_SPXBBinkOutstandingBytes -= s_binkAllocTrack[i].size;
			}
			if( g_SPXBBinkOutstandingCount > 0 )
			{
				--g_SPXBBinkOutstandingCount;
			}
			s_binkAllocTrack[i].ptr = NULL;
			s_binkAllocTrack[i].size = 0;
			return;
		}
	}
}

static void BinkTrackClearTemp(void)
{
	int i;
	unsigned int clearedCount = 0;
	unsigned int clearedBytes = 0;

	for( i = 0; i < (int)(sizeof(s_binkAllocTrack) / sizeof(s_binkAllocTrack[0])); ++i )
	{
		if( s_binkAllocTrack[i].ptr && Z_IsFromTempPool(s_binkAllocTrack[i].ptr) )
		{
			clearedBytes += s_binkAllocTrack[i].size;
			if( g_SPXBBinkOutstandingBytes >= s_binkAllocTrack[i].size )
			{
				g_SPXBBinkOutstandingBytes -= s_binkAllocTrack[i].size;
			}
			if( g_SPXBBinkOutstandingCount > 0 )
			{
				--g_SPXBBinkOutstandingCount;
			}
			s_binkAllocTrack[i].ptr = NULL;
			s_binkAllocTrack[i].size = 0;
			++clearedCount;
		}
	}

	XBLF("JA: BinkVideo::TrackClearTemp cleared=%u bytes=%u active=%u activeBytes=%u",
		clearedCount, clearedBytes,
		(unsigned int)g_SPXBBinkOutstandingCount,
		(unsigned int)g_SPXBBinkOutstandingBytes);
}
#endif

// Allocation wrappers, that go to our static 2.5MB buffer:
static void PTR4* RADEXPLINK AllocWrapper(U32 size)
{
#ifdef _XBOX
	static unsigned int s_binkAllocSeq = 0;
#endif
	void *retVal;

	// Give bink pre-initialized sound mem on xbox
	if(size == XBOX_BINK_SND_MEM) {
#ifdef _XBOX
		++s_binkAllocSeq;
		g_SPXBBinkAllocSeq = s_binkAllocSeq;
		g_SPXBBinkLastAllocSize = (unsigned int)size;
		g_SPXBBinkLastAllocPtr = (unsigned int)binkSndMem;
		BinkTrackAlloc(binkSndMem, size);
		XBLF("JA: BinkVideo::AllocWrapper #%u shared-sound size=%u ptr=%p active=%u bytes=%u",
			s_binkAllocSeq, (unsigned int)size, binkSndMem,
			(unsigned int)g_SPXBBinkOutstandingCount,
			(unsigned int)g_SPXBBinkOutstandingBytes);
#endif
		return binkSndMem;
	}

	retVal = BinkVideo::Allocate(size);
#ifdef _XBOX
	++s_binkAllocSeq;
	g_SPXBBinkAllocSeq = s_binkAllocSeq;
	g_SPXBBinkLastAllocSize = (unsigned int)size;
	g_SPXBBinkLastAllocPtr = (unsigned int)retVal;
	BinkTrackAlloc(retVal, size);
	XBLF("JA: BinkVideo::AllocWrapper #%u size=%u ptr=%p active=%u bytes=%u",
		s_binkAllocSeq, (unsigned int)size, retVal,
		(unsigned int)g_SPXBBinkOutstandingCount,
		(unsigned int)g_SPXBBinkOutstandingBytes);
#endif
	return retVal;
}

static void RADEXPLINK FreeWrapper(void PTR4* ptr)
{
#ifdef _XBOX
	static unsigned int s_binkFreeSeq = 0;
#endif
#ifdef _XBOX
	BinkTrackFree(ptr);
#endif
	if( ptr == binkSndMem )
	{
#ifdef _XBOX
		++s_binkFreeSeq;
		g_SPXBBinkFreeSeq = s_binkFreeSeq;
		g_SPXBBinkLastFreePtr = (unsigned int)ptr;
		XBLF("JA: BinkVideo::FreeWrapper #%u keeping shared Bink sound buffer ptr=%p active=%u bytes=%u",
			s_binkFreeSeq, ptr,
			(unsigned int)g_SPXBBinkOutstandingCount,
			(unsigned int)g_SPXBBinkOutstandingBytes);
#endif
		return;
	}

#ifdef _XBOX
	++s_binkFreeSeq;
	g_SPXBBinkFreeSeq = s_binkFreeSeq;
	g_SPXBBinkLastFreePtr = (unsigned int)ptr;
	XBLF("JA: BinkVideo::FreeWrapper #%u ptr=%p active=%u bytes=%u",
		s_binkFreeSeq, ptr,
		(unsigned int)g_SPXBBinkOutstandingCount,
		(unsigned int)g_SPXBBinkOutstandingBytes);
#endif
	BinkVideo::Free(ptr);
}

#ifdef _XBOX
static void BinkProbeFileOpenRead(const char *filename)
{
	g_SPXBBinkPhase = 70;
	if (!filename || !filename[0])
	{
		g_SPXBBinkPhase = 71;
		XBLog_Write("JA: BinkVideo::Probe skipped empty filename");
		return;
	}

	char binkLog[256];
	g_SPXBBinkPhase = 72;
	_snprintf(binkLog, sizeof(binkLog) - 1, "JA: BinkVideo::Probe before CreateFile file='%s'", filename);
	binkLog[sizeof(binkLog) - 1] = '\0';
	g_SPXBBinkPhase = 73;
	XBLog_Write(binkLog);

	g_SPXBBinkPhase = 74;
	HANDLE h = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	g_SPXBBinkPhase = 75;
	DWORD openErr = GetLastError();
	_snprintf(binkLog, sizeof(binkLog) - 1, "JA: BinkVideo::Probe after CreateFile handle=0x%08x err=%u", (unsigned int)h, (unsigned int)openErr);
	binkLog[sizeof(binkLog) - 1] = '\0';
	g_SPXBBinkPhase = 76;
	XBLog_Write(binkLog);

	if (h == INVALID_HANDLE_VALUE)
	{
		g_SPXBBinkPhase = 77;
		return;
	}

	unsigned char hdr[0x2c];
	DWORD bytesRead = 0;
	memset(hdr, 0, sizeof(hdr));
	g_SPXBBinkPhase = 78;
	BOOL readOk = ReadFile(h, hdr, sizeof(hdr), &bytesRead, NULL);
	g_SPXBBinkPhase = 79;
	DWORD readErr = GetLastError();
	_snprintf(binkLog, sizeof(binkLog) - 1,
		"JA: BinkVideo::Probe after ReadFile ok=%u bytes=%u err=%u first4=0x%02x%02x%02x%02x",
		(unsigned int)readOk, (unsigned int)bytesRead, (unsigned int)readErr,
		(unsigned int)hdr[0], (unsigned int)hdr[1], (unsigned int)hdr[2], (unsigned int)hdr[3]);
	binkLog[sizeof(binkLog) - 1] = '\0';
	g_SPXBBinkPhase = 80;
	XBLog_Write(binkLog);

	g_SPXBBinkPhase = 81;
	CloseHandle(h);
	g_SPXBBinkPhase = 82;
	XBLog_Write("JA: BinkVideo::Probe closed file");
	g_SPXBBinkPhase = 83;
}
#endif


/*********
BinkVideo
*********/
BinkVideo::BinkVideo()
{
	bink		= NULL;
	buffer		= NULL;
	x1			= 0.0f;
	y1			= 0.0f;
	x2			= 0.0f;
	y2			= 0.0f;
	status		= NS_BV_STOPPED;
	looping		= false;
	alpha		= false;
	initialized = false;
	loadScreenOnStop = false;

	Image[0].surface = NULL;
	Image[0].texture = NULL;
	overlayMemory[0] = NULL;
	Image[1].surface = NULL;
	Image[1].texture = NULL;
	overlayMemory[1] = NULL;

	stopNextFrame = false;
}

/*********
~BinkVideo
*********/
BinkVideo::~BinkVideo()
{
	Free(buffer);
	if( bink )
	{
		BinkClose(bink);
		bink = NULL;
	}
	if( overlayMemory[0] || overlayMemory[1] )
	{
		gTextures.UnswapTextureMemory();
		overlayMemory[0] = NULL;
		overlayMemory[1] = NULL;
	}
}

/*********
AllocateXboxMem
Pre-Allocates sound memory for xbox to avoid fragmenting
*********/
void BinkVideo::AllocateXboxMem(void)
{
	XBLog_Write("JA: BinkVideo::AllocateXboxMem enter");
//	binkSndMem = (char*)Allocate(XBOX_BINK_SND_MEM);
	// Force the sound memory to come from the Zone:
	binkSndMem = (char *) Z_Malloc(XBOX_BINK_SND_MEM, TAG_BINK, qfalse, 32);
	initialized = true;
	BinkLogMemoryState(10, "after-AllocateXboxMem");
	XBLog_Write("JA: BinkVideo::AllocateXboxMem exit");
}

/*********
FreeXboxMem
*********/
void BinkVideo::FreeXboxMem(void)
{
	XBLog_Write("JA: BinkVideo::FreeXboxMem enter");
	initialized = false;
	Z_Free(binkSndMem);
	binkSndMem = NULL;
	BinkLogMemoryState(20, "after-FreeXboxMem");
	XBLog_Write("JA: BinkVideo::FreeXboxMem exit");
}


/*********
Start
Opens a bink file and gets it ready to play
*********/
bool BinkVideo::Start(const char *filename, float xOrigin, float yOrigin, float width, float height, bool fullscreenMovie, bool inGameMovie)
{
#ifdef _XBOX
	g_SPXBCinPhase = 400;
	g_SPXBCinStatus = (unsigned int)status;
#endif
	char binkLog[256];
	_snprintf(binkLog, sizeof(binkLog) - 1, "JA: BinkVideo::Start enter file='%s' initialized=%d status=%d fullscreen=%d ingame=%d", filename ? filename : "<null>", initialized ? 1 : 0, status, fullscreenMovie ? 1 : 0, inGameMovie ? 1 : 0);
	binkLog[sizeof(binkLog) - 1] = '\0';
	XBLog_Write(binkLog);
	assert(initialized);
	BinkLogMemoryState(100, "Start-enter");

	// Check to see if a video is being played.
	if(status == NS_BV_PLAYING)
	{
		// stop
#ifdef _XBOX
		g_SPXBCinPhase = 401;
#endif
		this->Stop();
#ifdef _XBOX
		g_SPXBCinPhase = 402;
#endif
	}

	// Hack! Remember if this was the logo movies, so that we can show the load screen later:
	if( strstr( filename, "logos" ) )
		loadScreenOnStop = true;
	else
		loadScreenOnStop = false;

	// Blow away all sounds that aren't playing - this helps prevent crashing:
#ifdef _XBOX
	g_SPXBCinPhase = 403;
#endif
	SND_FreeOldestSound();
#ifdef _XBOX
	g_SPXBCinPhase = 404;
#endif
	BinkLogMemoryState(110, "after-SND_FreeOldestSound");

	// Just use the zone for bink allocations:
	RADSetMemory( AllocWrapper, FreeWrapper );
#ifdef _XBOX
	g_SPXBCinPhase = 405;
#endif
	XBLog_Write("JA: BinkVideo::Start after RADSetMemory");

	if( fullscreenMovie && glw_state && glw_state->device )
	{
		D3DVIEWPORT8 vp = { 0, 0, 640, 480, 0.0f, 1.0f };
#ifdef _XBOX
		g_SPXBCinPhase = 406;
		g_SPXBBinkPhase = 5;
#endif
		XBLog_Write("JA: BinkVideo::Start fullscreen clear/present");
		glw_state->device->SetViewport( &vp );
		glw_state->device->Clear( 0, 0, D3DCLEAR_TARGET, 0, 1.0f, 0 );
		glw_state->device->Present( 0, 0, 0, 0 );
	}

#if defined(_XBOX) && !defined(SP_XBOX_USE_XDEMO_BINK)
	static bool s_yuy2ConverterLoaded = false;
	if( fullscreenMovie )
	{
		if( !s_yuy2ConverterLoaded )
		{
			XBLog_Write("JA: BinkVideo::Start load RM4 YUY2 converter once");
			BinkUnloadConverter( BINKCONVERTERSALL );
			BinkLoadConverter( BINKSURFACEYUY2 );
			s_yuy2ConverterLoaded = true;
		}
		else
		{
			XBLog_Write("JA: BinkVideo::Start reuse RM4 YUY2 converter");
		}
		BinkLogMemoryState(115, "after-converter-setup");
	}
#endif

	// Set up sound for consoles

	// Retail JA SP opens Bink movies with BINKALPHA only. On this Xemu
	// experiment branch, BINKNOTHREADEDIO keeps RAD's background IO path from
	// idling forever before BinkOpen returns.
#if defined(SP_XBOX_XEMU_BINK_NOTHREADIO)
	XBLog_Write("JA: BinkVideo::Start using retail SP BINKALPHA + Xemu BINKNOTHREADEDIO open path");
#else
	XBLog_Write("JA: BinkVideo::Start using retail SP BINKALPHA open path");
#endif
	
	// Now route the sound tracks to the correct speaker
//	U32 bins[ 2 ];

//	bins[ 0 ] = DSMIXBIN_FRONT_LEFT;
//	bins[ 1 ] = DSMIXBIN_FRONT_RIGHT;
//	BinkSetMixBins( bink, 0, bins, 2 );
//	bins[ 0 ] = DSMIXBIN_FRONT_CENTER;
//	BinkSetMixBins( bink, 1, bins, 1 );
//	bins[ 0 ] = DSMIXBIN_LOW_FREQUENCY;
//	BinkSetMixBins( bink, 2, bins, 1 );
//	bins[ 0 ] = DSMIXBIN_BACK_LEFT;
//	bins[ 1 ] = DSMIXBIN_BACK_RIGHT;
//	BinkSetMixBins( bink, 3, bins, 2 );

	U32 binkOpenFlags = BINKALPHA;
#if defined(SP_XBOX_XEMU_BINK_NOTHREADIO)
	binkOpenFlags |= BINKNOTHREADEDIO;
#endif
	if( inGameMovie )
	{
		XBLog_Write("JA: BinkVideo::Start in-game path using normal SP Bink open flags");
	}

#ifdef _XBOX
	if( !filename || !filename[0] )
	{
		g_SPXBCinPhase = 414;
		g_SPXBBinkPhase = 86;
		g_SPXBBinkStartResult = 0xBAD00005;
		g_SPXBBinkStatus = (unsigned int)status;
		XBLog_Write("JA: BinkVideo::Start empty filename before BinkOpen");
		return false;
	}
	// Do not probe with a separate CreateFile here. Retail JA SP lets RAD own
	// the file open/IO lifecycle, and this path runs in very low memory.
	g_SPXBBinkPhase = 87;
	_snprintf(binkLog, sizeof(binkLog) - 1, "JA: BinkVideo::Start before direct BinkOpen file='%s' flags=0x%x", filename ? filename : "<null>", binkOpenFlags);
#else
	_snprintf(binkLog, sizeof(binkLog) - 1, "JA: BinkVideo::Start before BinkOpen file='%s' flags=0x%x", filename ? filename : "<null>", binkOpenFlags);
#endif
	binkLog[sizeof(binkLog) - 1] = '\0';
	XBLog_Write(binkLog);
#ifdef _XBOX
	g_SPXBCinPhase = 410;
	g_SPXBCinLoopCount = 0xB1000000;
	g_SPXBBinkPhase = 10;
	g_SPXBBinkRunCount = 0;
	g_SPXBBinkWaitLoops = 0;
	g_SPXBBinkWaitBreaks = 0;
	g_SPXBBinkFrameNum = 0;
	g_SPXBBinkFrames = 0;
	g_SPXBBinkOpenFlags = binkOpenFlags;
	g_SPXBBinkWidth = 0;
	g_SPXBBinkHeight = 0;
	g_SPXBBinkAlpha = 0;
	g_SPXBBinkCopySkipped = 0;
	g_SPXBBinkStartResult = 0;
	g_SPXBBinkStatus = (unsigned int)status;
#endif
#ifdef _XBOX
	BinkLogMemoryState(118, "before-system-reserve-release");
	Z_XboxReleaseSystemReserve("BinkOpen");
	BinkLogMemoryState(119, "after-system-reserve-release");
#endif
	BinkLogMemoryState(120, "before-BinkOpen");
	bink = BinkOpen( filename, binkOpenFlags );
#ifdef _XBOX
	g_SPXBCinPhase = 411;
	g_SPXBBinkPhase = bink ? 11 : 14;
#endif
	BinkLogMemoryState(130, "after-BinkOpen");
	if(!bink)
	{
#ifdef _XBOX
		g_SPXBCinPhase = 414;
		g_SPXBBinkStartResult = 0xBAD00001;
		g_SPXBBinkStatus = (unsigned int)status;
#endif
		_snprintf(binkLog, sizeof(binkLog) - 1, "JA: BinkVideo::Start BinkOpen failed file='%s' flags=0x%x err='%s'", filename ? filename : "<null>", binkOpenFlags, BinkGetError());
		binkLog[sizeof(binkLog) - 1] = '\0';
		XBLog_Write(binkLog);
#ifdef _XBOX
		Z_XboxRestoreSystemReserve("BinkOpen-failed");
		BinkLogMemoryState(131, "after-system-reserve-restore-failed-open");
#endif
		return false;
	}
	_snprintf(binkLog, sizeof(binkLog) - 1, "JA: BinkVideo::Start BinkOpen ok file='%s' w=%d h=%d frames=%d flags=0x%x", filename ? filename : "<null>", bink->Width, bink->Height, bink->Frames, bink->OpenFlags);
	binkLog[sizeof(binkLog) - 1] = '\0';
	XBLog_Write(binkLog);
#ifdef _XBOX
	g_SPXBCinPhase = 415;
	g_SPXBBinkPhase = 15;
	g_SPXBBinkFrameNum = bink->FrameNum;
	g_SPXBBinkFrames = bink->Frames;
	g_SPXBBinkOpenFlags = bink->OpenFlags;
	g_SPXBBinkWidth = bink->Width;
	g_SPXBBinkHeight = bink->Height;
#endif

	assert(bink->Width <= MAX_WIDTH && bink->Height <=MAX_HEIGHT);
#ifdef _XBOX
	g_SPXBBinkPhase = 16;
#endif

	// Did the source .bik file have an alpha plane?
	alpha = (bool)(bink->OpenFlags & BINKALPHA);
#ifdef _XBOX
	g_SPXBBinkPhase = 17;
	g_SPXBBinkAlpha = alpha ? 1 : 0;
#endif

	// set the height, width, etc...
	x1 = xOrigin;
	y1 = yOrigin;
	x2 = x1 + width;
	y2 = y1 + height;

	// flush any background sound reads
	extern void S_DrainRawSoundData(void);
#ifdef _XBOX
	g_SPXBCinPhase = 420;
	g_SPXBBinkPhase = 20;
#endif
	S_DrainRawSoundData();
#ifdef _XBOX
	g_SPXBCinPhase = 421;
	g_SPXBBinkPhase = 21;
#endif

	// Full-screen movies (without alpha) need a pair of YUV2 textures:
	if( !alpha )
	{
#ifdef _XBOX
		g_SPXBCinPhase = 430;
		g_SPXBBinkPhase = 30;
#endif
		if( !gTextures.IsInitialized() )
		{
#ifdef _XBOX
			g_SPXBBinkPhase = 38;
			g_SPXBBinkStartResult = 0xBAD00003;
			g_SPXBBinkStatus = (unsigned int)status;
#endif
			XBLog_Write("JA: BinkVideo::Start texture pool not initialized for overlay");
			BinkClose( bink );
			bink = NULL;
			return false;
		}

		unsigned long overlayBytes = (bink->Width * bink->Height * 4) + 1024;
		gTextures.SwapTextureMemory( overlayBytes );
#ifdef _XBOX
		g_SPXBCinPhase = 431;
		g_SPXBBinkPhase = 31;
#endif
		XBLF("JA: BinkVideo::Start swapped texture pool bytes=%u", (unsigned int)overlayBytes);

		Image[0].texture = new IDirect3DTexture8;
		Image[1].texture = new IDirect3DTexture8;
#ifdef _XBOX
		g_SPXBCinPhase = 432;
		g_SPXBBinkPhase = 32;
#endif
		DWORD pixelSize =
		XGSetTextureHeader( bink->Width,
							bink->Height,
							1,
							0,
							D3DFMT_YUY2,
							0,
							Image[0].texture,
							0,
							0 );

		XGSetTextureHeader( bink->Width,
							bink->Height,
							1,
							0,
							D3DFMT_YUY2,
							0,
							Image[1].texture,
							0,
							0 );

		overlayMemory[0] = gTextures.Allocate( pixelSize, 0 );
		overlayMemory[1] = gTextures.Allocate( pixelSize, 0 );
#ifdef _XBOX
		g_SPXBCinPhase = 433;
		g_SPXBBinkPhase = 33;
#endif
		XBLF("JA: BinkVideo::Start overlay header pixelSize=%u tex0=%p tex1=%p mem0=%p mem1=%p",
			(unsigned int)pixelSize, Image[0].texture, Image[1].texture, overlayMemory[0], overlayMemory[1]);
		if( !Image[0].texture || !Image[1].texture || !overlayMemory[0] || !overlayMemory[1] )
		{
#ifdef _XBOX
			g_SPXBBinkPhase = 39;
			g_SPXBBinkStartResult = 0xBAD00002;
			g_SPXBBinkStatus = (unsigned int)status;
#endif
			XBLog_Write("JA: BinkVideo::Start overlay header allocation failed");
			gTextures.UnswapTextureMemory();
			overlayMemory[0] = NULL;
			overlayMemory[1] = NULL;
			if( Image[0].texture )
			{
				delete Image[0].texture;
			}
			Image[0].texture = NULL;
			if( Image[1].texture )
			{
				delete Image[1].texture;
			}
			Image[1].texture = NULL;
			BinkClose( bink );
			bink = NULL;
			return false;
		}
		Image[0].texture->Register( overlayMemory[0] );
		Image[1].texture->Register( overlayMemory[1] );
#ifdef _XBOX
		g_SPXBCinPhase = 434;
		g_SPXBBinkPhase = 34;
#endif
		XBLog_Write("JA: BinkVideo::Start registered overlay texture memory");

		// Turn on overlays:
		glw_state->device->EnableOverlay( TRUE );
#ifdef _XBOX
		g_SPXBCinPhase = 435;
		g_SPXBBinkPhase = 35;
#endif
		XBLog_Write("JA: BinkVideo::Start enabled overlay");

		// Get surface pointers:
		Image[0].texture->GetSurfaceLevel( 0, &Image[0].surface );
		Image[1].texture->GetSurfaceLevel( 0, &Image[1].surface );
#ifdef _XBOX
		g_SPXBCinPhase = 436;
		g_SPXBBinkPhase = 36;
#endif
		XBLog_Write("JA: BinkVideo::Start got overlay surfaces");

		// Just to be safe:
		currentImage = 0;
		buffer = NULL;
	}
	else
	{
#ifdef _XBOX
		g_SPXBCinPhase = 440;
		g_SPXBBinkPhase = 40;
#endif
		// Planet movies (with alpha) re-use tr.binkPlanetImage, so no texture setup
		// is needed. But we do need a temporary buffer to decompress into. Let's steal
		// from the bone pool.
		buffer = BonePoolTempAlloc( bink->Width * bink->Height * 4 );
#ifdef _XBOX
		g_SPXBCinPhase = 441;
		g_SPXBBinkPhase = 41;
#endif
		XBLog_Write("JA: BinkVideo::Start allocated alpha movie buffer");
	}

#ifdef _XBOX
	g_SPXBBinkPhase = 49;
#endif
	status = NS_BV_PLAYING;
#ifdef _XBOX
	g_SPXBCinPhase = 450;
	g_SPXBCinStatus = (unsigned int)status;
	g_SPXBBinkPhase = 50;
	g_SPXBBinkStatus = (unsigned int)status;
	g_SPXBBinkStartResult = 1;
#endif
	XBLog_Write("JA: BinkVideo::Start exit playing");

	return true;
}

/*********
Run
Decompresses a frame, renders it to the screen, and advances to
the next frame. Only used for full-screen movies (no alpha).
*********/
bool BinkVideo::Run(void)
{
	static int runLogBudget = 12;
	// Make sure movie is running:
	if( status == NS_BV_STOPPED )
		return false;
	if( !bink )
	{
#ifdef _XBOX
		g_SPXBBinkPhase = 99;
		g_SPXBBinkStatus = (unsigned int)status;
#endif
		XBLog_Write("JA: BinkVideo::Run missing bink handle");
		status = NS_BV_STOPPED;
		return false;
	}

#ifdef _XBOX
	g_SPXBBinkPhase = 100;
	++g_SPXBBinkRunCount;
	g_SPXBBinkFrameNum = bink->FrameNum;
	g_SPXBBinkFrames = bink->Frames;
#endif
	if( runLogBudget > 0 )
	{
		XBLF("JA: BinkVideo::Run frame=%u/%u status=%d readerr=%u current=%d",
			(unsigned int)bink->FrameNum, (unsigned int)bink->Frames, (int)status,
			(unsigned int)bink->ReadError, currentImage);
		--runLogBudget;
	}

	// Are we supposed to stop now?
	if( stopNextFrame )
	{
#ifdef _XBOX
		g_SPXBBinkPhase = 115;
#endif
		XBLog_Write("JA: BinkVideo::Run stopNextFrame");
		stopNextFrame = false;
		Stop();
		return false;
	}

	// RM4/Jade's Xbox Bink player does not spin inside BinkWait. If the next
	// movie frame is not due yet, return to the engine so input, audio, and
	// render pumping can continue instead of freezing in a tight wait loop.
	if( BinkWait( bink ) )
	{
#ifdef _XBOX
		g_SPXBBinkPhase = 110;
		g_SPXBBinkWaitLoops = 1;
#endif
		return true;
	}
#ifdef _XBOX
	g_SPXBBinkPhase = 111;
	g_SPXBBinkWaitLoops = 0;
#endif

	// Try to decompress the frame:
	S32 copySkipped;
#ifdef _XBOX
	g_SPXBBinkPhase = 120;
#endif
	copySkipped = DecompressFrame( &Image[currentImage ^ 1] );
#ifdef _XBOX
	g_SPXBBinkCopySkipped = copySkipped;
	g_SPXBBinkPhase = 125;
#endif
	if( copySkipped == 0 )
	{
		// The blt succeeded, update our current image index.
		currentImage ^= 1;

		// Draw the next frame.
#ifdef _XBOX
		g_SPXBBinkPhase = 130;
#endif
		Draw( &Image[currentImage] );
#ifdef _XBOX
		g_SPXBBinkPhase = 135;
#endif
	}

	// Are we done? Set a flag, we don't want to stop until next frame, so the
	// last frame stays up for the right amount of time!
	if( bink->FrameNum == bink->Frames && !looping )
	{
		stopNextFrame = true;
	}

	// Keep playing:
#ifdef _XBOX
	g_SPXBBinkPhase = 140;
#endif
	BinkNextFrame( bink );
#ifdef _XBOX
	g_SPXBBinkPhase = 145;
	g_SPXBBinkFrameNum = bink ? bink->FrameNum : 0;
#endif
	if( bink && bink->ReadError )
	{
		XBLF("JA: BinkVideo::Run read error after next frame frame=%u/%u",
			(unsigned int)bink->FrameNum, (unsigned int)bink->Frames);
	}

	// Are we done?
/*
	if( bink->FrameNum == (bink->Frames - 1) && !looping )
	{
		Stop();
		return false;
	}
*/

	return true;
}

/*********
GetBinkData
Returns the buffer data for the next frame of the video - only used for
movies with alpha (the planets).

This doesn't follow Bink guidelines. They suggest that you call BinkWait()
very frequently, something like 4 to 5 times as fast as the framerate of
the movie. We're technically coming close to that, but this code won't work
if we have videoMap shaders with higher framerates than the planets (8).
*********/
void* BinkVideo::GetBinkData(void)
{
	assert( alpha );

	if (!BinkWait(bink))
	{
		BinkDoFrame(bink);

		BinkCopyToBuffer( bink,
						  buffer,
						  bink->Width * 4,	// Pitch
						  bink->Height,
						  0,
						  0,
						  BINKCOPYALL | BINKSURFACE32A );

		BinkNextFrame(bink);
	}

	return buffer;
}

/********
Draw
Draws the current movie full-screen
********/
void BinkVideo::Draw( OVERLAYINFO * oi )
{
	static int drawLogBudget = 8;
	// Draw the image on the screen (centered)...
	RECT dst_rect = { 0, 0, 640, 480 };
	RECT src_rect = { 0, 0, bink->Width, bink->Height };

	// Update this bugger.
	if( drawLogBudget > 0 )
	{
		XBLF("JA: BinkVideo::Draw overlay surface=%p src=%dx%d dst=640x480 frame=%u",
			oi ? oi->surface : NULL,
			bink ? (int)bink->Width : 0,
			bink ? (int)bink->Height : 0,
			bink ? (unsigned int)bink->FrameNum : 0);
		--drawLogBudget;
	}
	glw_state->device->UpdateOverlay( oi->surface, &src_rect, &dst_rect, FALSE, 0 );
}

/*********
Stop
Stops the current movie, and clears it from memory
*********/
void BinkVideo::Stop(void)
{
#ifdef _XBOX
	g_SPXBBinkPhase = 200;
#endif
	XBLog_Write("JA: BinkVideo::Stop enter");

	if( alpha )
	{
		// Release all the temp space we grabbed, no texture cleanup to do:
		if( buffer )
		{
			XBLog_Write("JA: BinkVideo::Stop free alpha buffer");
			BonePoolTempFree( buffer );
		}
	}
	else
	{
		if( Image[0].surface || Image[1].surface || Image[0].texture || Image[1].texture )
		{
			// Match the RAD Xbox overlay teardown order: wait, disable overlay, release resources.
			if( glw_state && glw_state->device )
			{
				XBLog_Write("JA: BinkVideo::Stop before vblank");
#ifdef _XBOX
				g_SPXBBinkPhase = 205;
#endif
				glw_state->device->BlockUntilVerticalBlank();

				XBLog_Write("JA: BinkVideo::Stop disabling overlay");
#ifdef _XBOX
				g_SPXBBinkPhase = 210;
#endif
				glw_state->device->EnableOverlay( FALSE );
			}
			else
			{
				XBLog_Write("JA: BinkVideo::Stop no D3D device for overlay disable");
			}

			XBLog_Write("JA: BinkVideo::Stop release overlay surfaces");
			if( Image[0].surface )
			{
				Image[0].surface->Release();
				Image[0].surface = NULL;
			}
			if( Image[1].surface )
			{
				Image[1].surface->Release();
				Image[1].surface = NULL;
			}

			XBLog_Write("JA: BinkVideo::Stop release overlay textures");
			if( Image[0].texture )
			{
				Image[0].texture->BlockUntilNotBusy();
				delete Image[0].texture;
				Image[0].texture = NULL;
			}
			if( Image[1].texture )
			{
				Image[1].texture->BlockUntilNotBusy();
				delete Image[1].texture;
				Image[1].texture = NULL;
			}

#ifdef _XBOX
			g_SPXBBinkPhase = 220;
#endif
			XBLog_Write("JA: BinkVideo::Stop restore texture pool overlay memory");
			gTextures.UnswapTextureMemory();
			overlayMemory[0] = NULL;
			overlayMemory[1] = NULL;
		}
	}

	if( bink )
	{
		XBLog_Write("JA: BinkVideo::Stop close bink");
#ifdef _XBOX
		g_SPXBBinkPhase = 225;
		BinkLogMemoryState(200, "before-BinkClose");
#endif
		BinkClose( bink );
		bink = NULL;
#ifdef _XBOX
		BinkLogMemoryState(210, "after-BinkClose");
#endif
	}

	x1		= 0.0f;
	y1		= 0.0f;
	x2		= 0.0f;
	y2		= 0.0f;
	buffer	= NULL;
	bink	= NULL;
	status	= NS_BV_STOPPED;

	// Now free all the temp memory that Bink took with it's internal allocations:
#ifdef _XBOX
	BinkTrackClearTemp();
#endif
	TempFree();
#ifdef _XBOX
	BinkLogMemoryState(220, "after-TempFree");
	Z_XboxRestoreSystemReserve("BinkStop");
	BinkLogMemoryState(230, "after-system-reserve-restore");
#endif

	if( !alpha && (cls.state == CA_CINEMATIC || cls.state == CA_ACTIVE) )
        re.InitDissolve(qfalse);

	stopNextFrame = false;
	currentImage = 0;
	looping = false;
	alpha = false;
	loadScreenOnStop = false;

#ifdef _XBOX
	g_SPXBBinkPhase = 230;
	g_SPXBBinkFrameNum = 0;
	g_SPXBBinkFrames = 0;
	g_SPXBBinkAlpha = 0;
#endif
	XBLog_Write("JA: BinkVideo::Stop exit");
}

/*********
SetExtents
Sets dimmension variables
*********/

void BinkVideo::SetExtents(float xOrigin, float yOrigin, float width, float height)
{
	x1 = xOrigin;
	y1 = yOrigin;
	x2 = x1 + width;
	y2 = y1 + height;
}

/*********
SetMasterVolume
Sets the volume of the specified track
*********/
void BinkVideo::SetMasterVolume(S32 volume)
{
#if defined(SP_XBOX_USE_XDEMO_BINK)
	BinkSetVolume(bink, volume);
#else
	BinkSetVolume(bink, 0, volume);
#endif
}

/*********
DecompressFrame
Decompresses current frame and copies the data to
the buffer
*********/
S32 BinkVideo::DecompressFrame( OVERLAYINFO *oi )
{
	static int copyLogBudget = 8;
	S32 copy_skipped;
	D3DLOCKED_RECT lock_rect;
	HRESULT lockHr;
	lock_rect.pBits = NULL;
	lock_rect.Pitch = 0;

	// Decompress the Bink frame.
#ifdef _XBOX
	g_SPXBBinkPhase = 300;
#endif
	BinkDoFrame( bink );
#ifdef _XBOX
	g_SPXBBinkPhase = 310;
#endif

	// Lock the 3D image so that we can copy the decompressed frame into it.
	lockHr = oi->texture->LockRect( 0, &lock_rect, 0, 0 );
#ifdef _XBOX
	g_SPXBBinkPhase = 320;
#endif
	if( FAILED(lockHr) || !lock_rect.pBits )
	{
		XBLF("JA: BinkVideo::DecompressFrame LockRect failed hr=0x%08x bits=%p frame=%u",
			(unsigned int)lockHr, lock_rect.pBits, bink ? (unsigned int)bink->FrameNum : 0);
		return 1;
	}

	// Copy the decompressed frame into the 3D image.
	copy_skipped = BinkCopyToBuffer( bink,
									 lock_rect.pBits,
									 lock_rect.Pitch,
									 bink->Height,
									 0, 0,
									 BINKSURFACEYUY2 | BINKCOPYALL );

	// Unlock the 3D image.
	oi->texture->UnlockRect( 0 );
#ifdef _XBOX
	g_SPXBBinkPhase = 330;
	g_SPXBBinkCopySkipped = copy_skipped;
#endif
	if( copyLogBudget > 0 )
	{
		XBLF("JA: BinkVideo::DecompressFrame frame=%u/%u pitch=%d bits=%p skipped=%d readerr=%u",
			bink ? (unsigned int)bink->FrameNum : 0,
			bink ? (unsigned int)bink->Frames : 0,
			(int)lock_rect.Pitch,
			lock_rect.pBits,
			(int)copy_skipped,
			bink ? (unsigned int)bink->ReadError : 0);
		--copyLogBudget;
	}

	return copy_skipped;
}

/*********
Allocate
Allocates memory for the frame buffer
*********/
void *BinkVideo::Allocate(U32 size)
{
	void *retVal = TempAlloc( size );

	// Fall back to Zone if we didn't get it
	if( !retVal )
		retVal = Z_Malloc(size, TAG_BINK, qfalse, 32);

	return retVal;
}

/*********
FreeBuffer
Releases the frame buffer memory
*********/
void BinkVideo::Free(void* ptr)
{
	// Did this pointer come from the Zone up above?
	if( !Z_IsFromTempPool( ptr ) )
		Z_Free(ptr);

	// Else, do nothing - we don't free temp allocations until movie is done
}
