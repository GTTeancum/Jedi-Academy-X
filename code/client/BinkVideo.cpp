/*
 * This version of BinkVideo.cpp now ONLY works on Xbox.
 * GCN support is hosed.
 */
#include "snd_local_console.h"
#include "../renderer/tr_local.h"
#include "BinkVideo.h"
#include "RAD.h"
#include "../win32/glw_win_dx8.h"
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
extern "C" volatile unsigned int g_SPXBCinBinkFrame;
extern "C" volatile unsigned int g_SPXBCinCopySkipped;
extern "C" volatile unsigned int g_SPXBCinRawSampleHash;
extern "C" volatile unsigned int g_SPXBCinRawSampleNonZero;
extern "C" volatile unsigned int g_SPXBCinOverlayStage;
extern "C" volatile unsigned int g_SPXBCinOverlayFrames;
extern "C" volatile unsigned int g_SPXBCinOverlayResult;
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
	currentImage = 0;

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
	ReleaseOverlay();
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

	// This static Xbox Bink build contains the YUY2 converter used by the Jade
	// retail player.  Its 32R entry point reports success but produces an empty
	// buffer, so full-screen movies must use the native YUY2 overlay path.
	BinkUnloadConverter( BINKCONVERTERSALL );
#ifdef _XBOX
	g_SPXBCinPhase = 406;
#endif
	BinkLoadConverter( BINKSURFACEYUY2 );
#ifdef _XBOX
	g_SPXBCinPhase = 407;
#endif
	XBLog_Write("JA: BinkVideo::Start loaded Xbox BINKSURFACEYUY2 converter");

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

	// Select the ordinary stereo/mono track before opening.  The Xbox Bink
	// runtime only creates its movie audio voice when BINKSNDTRACK is present.
	U32 trackId = 0;
	BinkSetSoundTrack( 1, &trackId );

	// Open the original retail BIK directly through the static Xbox decoder.
	_snprintf(binkLog, sizeof(binkLog) - 1, "JA: BinkVideo::Start before BinkOpen file='%s' flags=0x%x", filename ? filename : "<null>", BINKSNDTRACK);
	binkLog[sizeof(binkLog) - 1] = '\0';
	XBLog_Write(binkLog);
#ifdef _XBOX
	g_SPXBCinPhase = 410;
	g_SPXBCinLoopCount = 0xB1000000;
#endif
	bink = BinkOpen( filename, BINKSNDTRACK );
#ifdef _XBOX
	g_SPXBCinPhase = 411;
	g_SPXBCinLoopCount = 0xB1000001;
#endif
	if(!bink)
	{
		_snprintf(binkLog, sizeof(binkLog) - 1, "JA: BinkVideo::Start BinkOpen failed file='%s' flags=0x%x retry=0x%x", filename ? filename : "<null>", BINKSNDTRACK, BINKSNDTRACK | BINKALPHA);
		binkLog[sizeof(binkLog) - 1] = '\0';
		XBLog_Write(binkLog);
#ifdef _XBOX
		g_SPXBCinPhase = 412;
#endif
		bink = BinkOpen( filename, BINKSNDTRACK | BINKALPHA );
#ifdef _XBOX
		g_SPXBCinPhase = 413;
#endif
	}
	if(!bink)
	{
#ifdef _XBOX
		g_SPXBCinPhase = 414;
#endif
		_snprintf(binkLog, sizeof(binkLog) - 1, "JA: BinkVideo::Start BinkOpen failed file='%s' flags=0x%x", filename ? filename : "<null>", BINKSNDTRACK | BINKALPHA);
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
	if( alpha )
	{
		BinkUnloadConverter( BINKCONVERTERSALL );
		BinkLoadConverter( BINKSURFACE32A );
#ifdef _XBOX
		g_SPXBCinPhase = 416;
#endif
		XBLog_Write("JA: BinkVideo::Start loaded Xbox BINKSURFACE32A converter");
	}

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

	// The Xbox Bink library's YUY2 converter is the only converter in this build
	// that produces valid decoded pixels.  Decode into a linear YUY2 staging
	// area, convert it to RGBA, and let the renderer present the movie.  The
	// hardware-overlay route interprets this title's surface layout incorrectly
	// in XEMU and produces repeated triangular/striped corruption.
	if( !alpha )
	{
		U32 rgbaBytes;
		U32 yuy2Bytes;
#ifdef _XBOX
		g_SPXBCinPhase = 430;
		g_SPXBCinOverlayStage = 0x59520001; /* 'YR' allocate raw buffers */
#endif
		rgbaBytes = bink->Width * bink->Height * 4;
		yuy2Bytes = bink->Width * bink->Height * 2;
		buffer = BonePoolTempAlloc( rgbaBytes + yuy2Bytes );
#ifdef _XBOX
		g_SPXBCinPhase = 431;
		g_SPXBCinOverlayResult = buffer ? 0 : 0x8007000e;
#endif
		if( !buffer )
		{
			XBLF("STEFX: BinkVideo::Start YUY2/RGBA allocation failed bytes=%u w=%u h=%u",
				rgbaBytes + yuy2Bytes, bink->Width, bink->Height);
			BinkClose( bink );
			bink = NULL;
			return false;
		}
		overlayMemory[0] = (byte *)buffer + rgbaBytes;
		overlayMemory[1] = NULL;
		currentImage = 0;
#ifdef _XBOX
		g_SPXBCinOverlayStage = 0x59520002; /* staging buffers ready */
#endif
		XBLF("STEFX: BinkVideo::Start using YUY2-to-RGBA renderer path w=%u h=%u bytes=%u",
			bink->Width, bink->Height, rgbaBytes + yuy2Bytes);
	}
	else
	{
#ifdef _XBOX
		g_SPXBCinPhase = 440;
#endif
		// Planet movies (with alpha) re-use tr.binkPlanetImage, so no texture setup
		// is needed. But we do need a temporary buffer to decompress into.
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

	// Make sure movie is running:
	if( status == NS_BV_STOPPED )
		return false;

#ifdef _XBOX
	g_SPXBCinBinkFrame = bink ? bink->FrameNum : 0;
#endif

	// BinkWait is a poll, not a blocking wait.  Return to the cinematic pump so
	// input and audio continue while the next frame matures.  The old hardware
	// overlay retained its last image automatically; the renderer path must
	// explicitly submit that image again because each backbuffer is cleared.
	if ( BinkWait( bink ) )
	{
		if( currentImage != 0 )
		{
			Draw( NULL, false );
		}
		return true;
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

	// Decode YUY2, convert to RGBA, and present through the normal renderer.
	if( DecompressFrame( NULL ) == 0 )
	{
		currentImage = 1;
		Draw( NULL, true );
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
#ifdef _XBOX
	g_SPXBCinBinkFrame = bink ? bink->FrameNum : 0;
#endif

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

void BinkVideo::Draw( OVERLAYINFO *oi, bool dirty )
{
	if( !bink || !buffer )
	{
		return;
	}

	re.DrawStretchRaw( (int)x1, (int)y1, (int)(x2 - x1), (int)(y2 - y1),
		bink->Width, bink->Height, (const byte *)buffer, 0, dirty ? qtrue : qfalse );
#ifdef _XBOX
	g_SPXBCinOverlayResult = 0;
	++g_SPXBCinOverlayFrames;
	g_SPXBCinOverlayStage = 0x59520009; /* RGBA frame submitted */
#endif
}

/*********
Stop
Stops the current movie, and clears it from memory
*********/
void BinkVideo::Stop(void)
{
	XBLog_Write("JA: BinkVideo::Stop enter");
	bool hadMovie = (bink != NULL) || (buffer != NULL) ||
		(Image[0].texture != NULL) || (status != NS_BV_STOPPED);

	if( bink ) {
		BinkClose( bink );
		bink = NULL;
	}

	if( buffer )
	{
		BonePoolTempFree( buffer );
		buffer = NULL;
		overlayMemory[0] = NULL;
		overlayMemory[1] = NULL;
	}

	if( !alpha )
	{
		ReleaseOverlay();
	}

	if( hadMovie && !alpha )
	{
#ifdef _XBOX
		XBLF("STEFX: BinkVideo::Stop overlay path loadScreenOnStop=%d", loadScreenOnStop ? 1 : 0);
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
	currentImage = 0;

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
		BinkSetVolume(bink,i,volume);
	}
}

S32 BinkVideo::DecompressFrame( OVERLAYINFO *oi )
{
	S32 copy_skipped;
	const byte *srcBase;
	byte *dstBase;
	unsigned int y;
	static unsigned int s_overlayCopyLogBudget = 8;

	if( !bink || !buffer || !overlayMemory[0] )
	{
		return 1;
	}

	BinkDoFrame( bink );
#ifdef _XBOX
	g_SPXBCinOverlayStage = 0x59520006; /* frame decompressed */
#endif

	copy_skipped = BinkCopyToBuffer( bink,
									 overlayMemory[0],
									 bink->Width * 2,
									 bink->Height,
									 0, 0,
									 BINKSURFACEYUY2 | BINKCOPYALL );

	// YUY2 stores two pixels as Y0,U,Y1,V.  Convert with the standard studio-
	// range coefficients used by Xbox video hardware and clamp to RGBA8.
	srcBase = (const byte *)overlayMemory[0];
	dstBase = (byte *)buffer;
	for( y = 0; y < bink->Height; ++y )
	{
		const byte *src = srcBase + y * bink->Width * 2;
		byte *dst = dstBase + y * bink->Width * 4;
		unsigned int x;
		for( x = 0; x + 1 < bink->Width; x += 2 )
		{
			const int y0 = (int)src[0] - 16;
			const int u = (int)src[1] - 128;
			const int y1v = (int)src[2] - 16;
			const int v = (int)src[3] - 128;
			int r0 = (298 * y0 + 409 * v + 128) >> 8;
			int g0 = (298 * y0 - 100 * u - 208 * v + 128) >> 8;
			int b0 = (298 * y0 + 516 * u + 128) >> 8;
			int r1 = (298 * y1v + 409 * v + 128) >> 8;
			int g1 = (298 * y1v - 100 * u - 208 * v + 128) >> 8;
			int b1 = (298 * y1v + 516 * u + 128) >> 8;

			if( r0 < 0 ) r0 = 0; else if( r0 > 255 ) r0 = 255;
			if( g0 < 0 ) g0 = 0; else if( g0 > 255 ) g0 = 255;
			if( b0 < 0 ) b0 = 0; else if( b0 > 255 ) b0 = 255;
			if( r1 < 0 ) r1 = 0; else if( r1 > 255 ) r1 = 255;
			if( g1 < 0 ) g1 = 0; else if( g1 > 255 ) g1 = 255;
			if( b1 < 0 ) b1 = 0; else if( b1 > 255 ) b1 = 255;

			dst[0] = (byte)r0; dst[1] = (byte)g0; dst[2] = (byte)b0; dst[3] = 255;
			dst[4] = (byte)r1; dst[5] = (byte)g1; dst[6] = (byte)b1; dst[7] = 255;
			src += 4;
			dst += 8;
		}
	}

#ifdef _XBOX
	{
		const unsigned int *pixels = (const unsigned int *)buffer;
		const unsigned int wordCount = (bink->Width * bink->Height * 4) / sizeof(unsigned int);
		const unsigned int sampleStride = (wordCount >= 1024) ? (wordCount / 1024) : 1;
		unsigned int sampleHash = 2166136261u;
		unsigned int sampleNonZero = 0;
		unsigned int sampleIndex;

		for( sampleIndex = 0; sampleIndex < wordCount; sampleIndex += sampleStride )
		{
			const unsigned int pixel = pixels[sampleIndex];
			sampleHash ^= pixel;
			sampleHash *= 16777619u;
			if( pixel != 0 )
			{
				++sampleNonZero;
			}
		}
		g_SPXBCinCopySkipped = (unsigned int)copy_skipped;
		g_SPXBCinRawSampleHash = sampleHash;
		g_SPXBCinRawSampleNonZero = sampleNonZero;
		g_SPXBCinOverlayStage = 0x59520008; /* RGBA conversion complete */
		g_SPXBCinOverlayResult = 0;
	}
#endif

	if( s_overlayCopyLogBudget > 0 )
	{
		XBLF("STEFX: BinkVideo::DecompressFrame frame=%u/%u pitch=%u copySkipped=%d surface=YUY2-to-RGBA hash=0x%08x nonzero=%u",
			bink->FrameNum, bink->Frames, bink->Width * 2, copy_skipped,
			(unsigned int)g_SPXBCinRawSampleHash,
			(unsigned int)g_SPXBCinRawSampleNonZero);
		--s_overlayCopyLogBudget;
	}

	return copy_skipped;
}

void BinkVideo::ReleaseOverlay( void )
{
	int i;
	bool hadOverlay = false;

	for( i = 0; i < 2; ++i )
	{
		if( Image[i].surface || Image[i].texture )
		{
			hadOverlay = true;
		}
		if( Image[i].surface )
		{
			Image[i].surface->Release();
			Image[i].surface = NULL;
		}
		if( Image[i].texture )
		{
			Image[i].texture->BlockUntilNotBusy();
			Image[i].texture->Release();
			Image[i].texture = NULL;
		}
	}

	if( hadOverlay && glw_state && glw_state->device )
	{
		glw_state->device->EnableOverlay( FALSE );
	}

	overlayMemory[0] = NULL;
	overlayMemory[1] = NULL;
#ifdef _XBOX
	g_SPXBCinOverlayStage = 0x4F56000A; /* overlay released */
#endif
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
