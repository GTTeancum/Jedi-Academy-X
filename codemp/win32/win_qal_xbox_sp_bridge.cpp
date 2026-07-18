#include <xtl.h>
#include <dsound.h>

static LPDSEFFECTIMAGEDESC s_stefxEffectsImageDesc = NULL;

static LPDSEFFECTIMAGEDESC *STEFX_CaptureEffectsImageDesc( LPDSEFFECTIMAGEDESC * )
{
	return &s_stefxEffectsImageDesc;
}

#define DownloadEffectsImage(image, length, effect, imageDesc) \
	DownloadEffectsImage(image, length, effect, STEFX_CaptureEffectsImageDesc(imageDesc))
#include "win_qal_xbox.cpp"
#undef DownloadEffectsImage

LPDSEFFECTIMAGEDESC getEffectsImageDesc( void )
{
	return s_stefxEffectsImageDesc;
}

void Sys_StreamRequestQueueClear( void )
{
	if ( !s_pState || !s_pState->m_Stream.m_Valid )
	{
		return;
	}

	WaitForSingleObject( s_pState->m_Stream.m_Mutex, INFINITE );
	s_pState->m_Stream.m_Queue.clear();
	ReleaseMutex( s_pState->m_Stream.m_Mutex );
}
