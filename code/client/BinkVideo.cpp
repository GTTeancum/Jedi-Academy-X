/*
 * Temporary Xbox stub: disable Bink playback while the vc71/XDK build path
 * is being migrated. Keep the interface intact and log attempted use.
 */
#include "BinkVideo.h"
#include "../win32/xb_log.h"
#include <stdio.h>

static void BinkStubLog(const char* msg)
{
	XBLog_Write(msg);
}

BinkVideo::BinkVideo()
{
	bink = NULL;
	buffer = NULL;
	status = NS_BV_STOPPED;
	looping = false;
	alpha = false;
	x1 = y1 = x2 = y2 = 0.0f;
	loadScreenOnStop = false;
	stopNextFrame = false;
	currentImage = 0;
	initialized = false;
	Image[0].texture = NULL;
	Image[0].surface = NULL;
	Image[1].texture = NULL;
	Image[1].surface = NULL;
}

BinkVideo::~BinkVideo()
{
}

void BinkVideo::AllocateXboxMem(void)
{
	initialized = true;
	BinkStubLog("BinkVideo stub: AllocateXboxMem");
}

void BinkVideo::FreeXboxMem(void)
{
	initialized = false;
	BinkStubLog("BinkVideo stub: FreeXboxMem");
}

bool BinkVideo::Start(const char *filename, float xOrigin, float yOrigin, float width, float height)
{
	x1 = xOrigin;
	y1 = yOrigin;
	x2 = x1 + width;
	y2 = y1 + height;
	status = NS_BV_STOPPED;
	loadScreenOnStop = false;
	stopNextFrame = false;

	if (filename && *filename)
	{
		char msg[256];
		_snprintf(msg, sizeof(msg) - 1, "BinkVideo stub: Start requested for %s", filename);
		msg[sizeof(msg) - 1] = '\0';
		XBLog_Write(msg);
	}
	else
	{
		BinkStubLog("BinkVideo stub: Start requested for <null>");
	}

	return false;
}

bool BinkVideo::Run(void)
{
	if (status != NS_BV_STOPPED)
	{
		BinkStubLog("BinkVideo stub: Run while active");
	}
	return false;
}

void* BinkVideo::GetBinkData(void)
{
	BinkStubLog("BinkVideo stub: GetBinkData");
	return NULL;
}

void BinkVideo::Draw( OVERLAYINFO * )
{
}

void BinkVideo::Stop(void)
{
	if (status != NS_BV_STOPPED)
	{
		BinkStubLog("BinkVideo stub: Stop");
	}
	status = NS_BV_STOPPED;
	buffer = NULL;
	bink = NULL;
}

void BinkVideo::SetExtents(float xOrigin, float yOrigin, float width, float height)
{
	x1 = xOrigin;
	y1 = yOrigin;
	x2 = x1 + width;
	y2 = y1 + height;
}

void BinkVideo::SetMasterVolume(S32)
{
}

S32 BinkVideo::DecompressFrame( OVERLAYINFO * )
{
	BinkStubLog("BinkVideo stub: DecompressFrame");
	return -1;
}

void *BinkVideo::Allocate(U32)
{
	return NULL;
}

void BinkVideo::Free(void*)
{
}
