/*
 * This version of BinkVideo.cpp now ONLY works on Xbox.
 * GCN support is hosed.
 */
#include "snd_local_console.h"
#include "../renderer/tr_local.h"
#include "BinkVideo.h"
#include "RAD.h"
#include "../win32/xb_log.h"

#include "../client/client.h"

char *binkSndMem = NULL;

// Taste the hackery!
extern void *BonePoolTempAlloc( unsigned long size );
extern void BonePoolTempFree( void *p );
extern void *TempAlloc( unsigned long size );
extern void TempFree( void );

extern void SP_DrawSPLoadScreen( void );

static bool BinkVideo_IsEFFrontendIntroMovie(const char *filename)
{
	if (!filename || !filename[0])
	{
		return false;
	}

	const char *base = filename;
	const char *scan;
	for (scan = filename; *scan; ++scan)
	{
		if (*scan == '\\' || *scan == '/' || *scan == ':')
		{
			base = scan + 1;
		}
	}

	return !Q_stricmp(base, "intro.bik") || !Q_stricmp(base, "intro") ||
		!Q_stricmp(base, "intro_lo.bik") || !Q_stricmp(base, "intro_lo");
}

#ifdef _XBOX
extern "C" volatile unsigned int g_SPXBCinPhase;
extern "C" volatile unsigned int g_SPXBCinStatus;
extern "C" volatile unsigned int g_SPXBCinLoopCount;
#endif

// Allocation wrappers, that go to our static 2.5MB buffer:
static void PTR4* RADEXPLINK AllocWrapper(U32 size)
{
	// Give bink pre-initialized sound mem on xbox
	if(size == XBOX_BINK_SND_MEM) {
		return binkSndMem;
	}

	return BinkVideo::Allocate(size);
}

static void RADEXPLINK FreeWrapper(void PTR4* ptr)
{
	BinkVideo::Free(ptr);
}


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


	stopNextFrame = false;
}

/*********
~BinkVideo
*********/
BinkVideo::~BinkVideo()
{
	if( buffer )
	{
		BonePoolTempFree( buffer );
		buffer = NULL;
	}
	if( bink )
	{
		BinkClose( bink );
		bink = NULL;
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
	XBLog_Write("JA: BinkVideo::FreeXboxMem exit");
}


/*********
Start
Opens a bink file and gets it ready to play
*********/
bool BinkVideo::Start(const char *filename, float xOrigin, float yOrigin, float width, float height)
{
#ifdef _XBOX
	g_SPXBCinPhase = 400;
	g_SPXBCinStatus = (unsigned int)status;
#endif
	char binkLog[256];
	_snprintf(binkLog, sizeof(binkLog) - 1, "JA: BinkVideo::Start enter file='%s' initialized=%d status=%d", filename ? filename : "<null>", initialized ? 1 : 0, status);
	binkLog[sizeof(binkLog) - 1] = '\0';
	XBLog_Write(binkLog);
	assert(initialized);

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

	// EF owns the frontend movie path; only the final EF intro movie hands off to loading.
	if( BinkVideo_IsEFFrontendIntroMovie( filename ) )
	{
		loadScreenOnStop = true;
	}
	else
	{
		loadScreenOnStop = false;
	}
#ifdef _XBOX
	XBLF("STEFX: BinkVideo::Start loadScreenOnStop=%d file='%s'",
		loadScreenOnStop ? 1 : 0,
		filename ? filename : "<null>");
#endif

	// Blow away all sounds that aren't playing - this helps prevent crashing:
#ifdef _XBOX
	g_SPXBCinPhase = 403;
#endif
	SND_FreeOldestSound();
#ifdef _XBOX
	g_SPXBCinPhase = 404;
#endif

	// Just use the zone for bink allocations:
	RADSetMemory( AllocWrapper, FreeWrapper );
#ifdef _XBOX
	g_SPXBCinPhase = 405;
#endif
	XBLog_Write("JA: BinkVideo::Start after RADSetMemory");

	XBLog_Write("JA: BinkVideo::Start using static Xbox Bink converters");

	// Set up sound for consoles

	// We are on XBox, tell Bink to play all of the 5.1 tracks
//	U32 TrackIDsToPlay[ 4 ] = { 0, 1, 2, 3 };	
//	BinkSetSoundTrack( 4, TrackIDsToPlay );
	
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

	// Try to open the Bink file.
//	bink = BinkOpen( filename, BINKSNDTRACK | BINKALPHA );
	_snprintf(binkLog, sizeof(binkLog) - 1, "JA: BinkVideo::Start before BinkOpen file='%s' flags=0x0", filename ? filename : "<null>");
	binkLog[sizeof(binkLog) - 1] = '\0';
	XBLog_Write(binkLog);
#ifdef _XBOX
	g_SPXBCinPhase = 410;
	g_SPXBCinLoopCount = 0xB1000000;
#endif
	bink = BinkOpen( filename, 0 );
#ifdef _XBOX
	g_SPXBCinPhase = 411;
	g_SPXBCinLoopCount = 0xB1000001;
#endif
	if(!bink)
	{
		_snprintf(binkLog, sizeof(binkLog) - 1, "JA: BinkVideo::Start BinkOpen failed file='%s' flags=0x0 retry=0x%x", filename ? filename : "<null>", BINKALPHA);
		binkLog[sizeof(binkLog) - 1] = '\0';
		XBLog_Write(binkLog);
#ifdef _XBOX
		g_SPXBCinPhase = 412;
#endif
		bink = BinkOpen( filename, BINKALPHA );
#ifdef _XBOX
		g_SPXBCinPhase = 413;
#endif
	}
	if(!bink)
	{
#ifdef _XBOX
		g_SPXBCinPhase = 414;
#endif
		_snprintf(binkLog, sizeof(binkLog) - 1, "JA: BinkVideo::Start BinkOpen failed file='%s' flags=0x%x", filename ? filename : "<null>", BINKALPHA);
		binkLog[sizeof(binkLog) - 1] = '\0';
		XBLog_Write(binkLog);
		return false;
	}
	_snprintf(binkLog, sizeof(binkLog) - 1, "JA: BinkVideo::Start BinkOpen ok file='%s' w=%d h=%d frames=%d flags=0x%x", filename ? filename : "<null>", bink->Width, bink->Height, bink->Frames, bink->OpenFlags);
	binkLog[sizeof(binkLog) - 1] = '\0';
	XBLog_Write(binkLog);
#ifdef _XBOX
	g_SPXBCinPhase = 415;
#endif

	assert(bink->Width <= MAX_WIDTH && bink->Height <=MAX_HEIGHT);

	// Did the source .bik file have an alpha plane?
	alpha = (bool)(bink->OpenFlags & BINKALPHA);

	// set the height, width, etc...
	x1 = xOrigin;
	y1 = yOrigin;
	x2 = x1 + width;
	y2 = y1 + height;

	// flush any background sound reads
	extern void S_DrainRawSoundData(void);
#ifdef _XBOX
	g_SPXBCinPhase = 420;
#endif
	S_DrainRawSoundData();
#ifdef _XBOX
	g_SPXBCinPhase = 421;
#endif

	// Full-screen EF movies use the renderer scratch texture path.  The inherited
	// JA YUY2 overlay path presents as flat green on the current Xbox renderer.
	if( !alpha )
	{
		U32 frameBytes;
#ifdef _XBOX
		g_SPXBCinPhase = 430;
#endif
		frameBytes = bink->Width * bink->Height * 4;
		buffer = BonePoolTempAlloc( frameBytes );
#ifdef _XBOX
		g_SPXBCinPhase = 431;
#endif
		if( !buffer )
		{
			XBLF("STEFX: BinkVideo::Start raw frame buffer allocation failed bytes=%u w=%u h=%u",
				frameBytes, bink->Width, bink->Height);
			BinkClose( bink );
			bink = NULL;
			return false;
		}

		XBLF("STEFX: BinkVideo::Start using EF raw frame path w=%u h=%u bytes=%u",
			bink->Width, bink->Height, frameBytes);
	}
	else
	{
#ifdef _XBOX
		g_SPXBCinPhase = 440;
#endif
		// Planet movies (with alpha) re-use tr.binkPlanetImage, so no texture setup
		// is needed. But we do need a temporary buffer to decompress into. Let's steal
		// from the bone pool.
		buffer = BonePoolTempAlloc( bink->Width * bink->Height * 4 );
#ifdef _XBOX
		g_SPXBCinPhase = 441;
#endif
		XBLog_Write("JA: BinkVideo::Start allocated alpha movie buffer");
	}

	status = NS_BV_PLAYING;
#ifdef _XBOX
	g_SPXBCinPhase = 450;
	g_SPXBCinStatus = (unsigned int)status;
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
	static unsigned int s_binkRunCalls = 0;
	static unsigned int s_binkWaitSpinWarnings = 0;

	// Make sure movie is running:
	if( status == NS_BV_STOPPED )
		return false;

	// Wait for proper frame timing:
	unsigned int waitSpins = 0;
	while(BinkWait(bink))
	{
		++waitSpins;
		if (waitSpins > 2000000)
		{
			if (s_binkWaitSpinWarnings < 8)
			{
				XBLF("STEFX: BinkVideo::Run wait guard frame=%u/%u spins=%u status=%d",
					bink ? bink->FrameNum : 0, bink ? bink->Frames : 0, waitSpins, status);
				++s_binkWaitSpinWarnings;
			}
			break;
		}
	}
	++s_binkRunCalls;
	if (s_binkRunCalls <= 4 || (s_binkRunCalls % 120) == 0)
	{
		XBLF("STEFX: BinkVideo::Run frame=%u/%u calls=%u status=%d looping=%d stopNext=%d",
			bink ? bink->FrameNum : 0, bink ? bink->Frames : 0, s_binkRunCalls,
			status, looping ? 1 : 0, stopNextFrame ? 1 : 0);
	}

	// Are we supposed to stop now?
	if( stopNextFrame )
	{
		XBLog_Write("JA: BinkVideo::Run stopNextFrame");
		stopNextFrame = false;
		Stop();
		return false;
	}

	// Try to decompress and draw the frame through the EF renderer path.
	if( DecompressFrameToBuffer() == 0 )
	{
		DrawFrameBuffer();
	}

	// Are we done? Set a flag, we don't want to stop until next frame, so the
	// last frame stays up for the right amount of time!
	if( bink->FrameNum == bink->Frames && !looping )
	{
		XBLF("STEFX: BinkVideo::Run reached final frame=%u/%u", bink->FrameNum, bink->Frames);
		stopNextFrame = true;
	}

	// Keep playing:
	BinkNextFrame( bink );

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

void BinkVideo::DrawFrameBuffer( void )
{
	if( !bink || !buffer )
	{
		return;
	}

	re.DrawStretchRaw( (int)x1, (int)y1, (int)(x2 - x1), (int)(y2 - y1),
		bink->Width, bink->Height, (const byte *)buffer, 0, qtrue );
}

/*********
Stop
Stops the current movie, and clears it from memory
*********/
void BinkVideo::Stop(void)
{
	XBLog_Write("JA: BinkVideo::Stop enter");
	bool hadMovie = (bink != NULL) || (buffer != NULL) || (status != NS_BV_STOPPED);

	if( bink ) {
		BinkClose( bink );
		bink = NULL;
	}

	if( buffer )
	{
		BonePoolTempFree( buffer );
		buffer = NULL;
	}

	if( hadMovie && !alpha )
	{
#ifdef _XBOX
		XBLF("STEFX: BinkVideo::Stop raw path loadScreenOnStop=%d", loadScreenOnStop ? 1 : 0);
#endif
		if( loadScreenOnStop )
		{
#ifdef _XBOX
			XBLog_Write("STEFX: BinkVideo::Stop drawing EF SP load screen after frontend intro");
#endif
			SP_DrawSPLoadScreen();
#ifdef _XBOX
			XBLog_Write("STEFX: BinkVideo::Stop flushing EF SP load screen after frontend intro");
#endif
			re.EndFrame( NULL, NULL );
#ifdef _XBOX
			XBLog_Write("STEFX: BinkVideo::Stop EF SP load screen flush done");
#endif
		}
		else
		{
#ifdef _XBOX
			XBLog_Write("STEFX: BinkVideo::Stop no post-movie clear; renderer owns presentation");
#endif
		}
	}

	x1		= 0.0f;
	y1		= 0.0f;
	x2		= 0.0f;
	y2		= 0.0f;
	buffer	= NULL;
	bink	= NULL;
	status	= NS_BV_STOPPED;

	// Now free all the temp memory that Bink took with it's internal allocations:
	TempFree();

	if( !alpha && (cls.state == CA_CINEMATIC || cls.state == CA_ACTIVE) )
        re.InitDissolve(qfalse);
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
	int i;
	for(i = 0; i < 4; i++)
	{
#ifdef _XBOX
		BinkSetVolume(bink,volume);
#else
		BinkSetVolume(bink,i,volume);
#endif
	}
}

S32 BinkVideo::DecompressFrameToBuffer( void )
{
	S32 copy_skipped;
	static unsigned int s_rawCopyLogBudget = 8;

	if( !bink || !buffer )
	{
		return 1;
	}

	BinkDoFrame( bink );

	copy_skipped = BinkCopyToBuffer( bink,
									 buffer,
									 bink->Width * 4,
									 bink->Height,
									 0, 0,
									 BINKSURFACE32R | BINKCOPYNOSCALING );

	if( s_rawCopyLogBudget > 0 )
	{
		XBLF("STEFX: BinkVideo::DecompressFrameToBuffer frame=%u/%u pitch=%u copySkipped=%d surface=32R noscale",
			bink->FrameNum, bink->Frames, bink->Width * 4, copy_skipped);
		--s_rawCopyLogBudget;
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
