
/*
 * UNPUBLISHED -- Rights  reserved  under  the  copyright  laws  of the 
 * United States.  Use  of a copyright notice is precautionary only and 
 * does not imply publication or disclosure.                            
 *                                                                      
 * THIS DOCUMENTATION CONTAINS CONFIDENTIAL AND PROPRIETARY INFORMATION 
 * OF    VICARIOUS   VISIONS,  INC.    ANY  DUPLICATION,  MODIFICATION, 
 * DISTRIBUTION, OR DISCLOSURE IS STRICTLY PROHIBITED WITHOUT THE PRIOR 
 * EXPRESS WRITTEN PERMISSION OF VICARIOUS VISIONS, INC.
 */

#ifndef __XBOX_TEXTURE_MAN_H__
#define __XBOX_TEXTURE_MAN_H__

#include "glw_win_dx8.h"
#include <xtl.h>


// Texture allocator that never frees anything, just grows until it's totally reset:
class StaticTextureAllocator
{
public:
	StaticTextureAllocator( void )
		: base(NULL),
		  allocPoint(0),
		  poolSize(0),
		  maxAlloc(0),
		  swappedSize(0),
		  swappedPoint(0),
		  swappedBackup(NULL)
	{
	}

	void Initialize( unsigned long size )
	{
		if ( base )
			return;

		base = (unsigned char *) D3D_AllocContiguousMemory( size, 0 );
		allocPoint = 0;
		poolSize = size;
		maxAlloc = 0;
		swappedSize = 0;
		swappedPoint = 0;
		swappedBackup = NULL;
	}

	bool IsInitialized( void ) const
	{
		return base != NULL && poolSize != 0;
	}

	// No bookkeeping necessary, texNum is unused:
	void *Allocate( unsigned long size, GLuint texNum )
	{
		if( !IsInitialized() )
		{
			OutputDebugStringA("JA: StaticTextureAllocator used before Initialize\n");
			throw "Static texture pool uninitialized";
		}
		if( allocPoint + size > poolSize )
		{
			OutputDebugStringA("JA: StaticTextureAllocator pool full\n");
			throw "Static texture pool full";
		}

		// Current location:
		void *retVal = base + allocPoint;

		// Advance, then round up:
		allocPoint += size;
		allocPoint = (allocPoint + 127) & ~127;

#ifndef FINAL_BUILD
		if( allocPoint > maxAlloc )
			maxAlloc = allocPoint;
#endif

		return retVal;
	}

	void Reset( void )
	{
		if ( !IsInitialized() )
			return;

		// Just move our allocation marker back to the start:
		allocPoint = 0;
	}

	// This is used by the bink code to make room for a giant texture
	// that doesn't need to live at the same time as any others
	void SwapTextureMemory( unsigned long size )
	{
		assert( !swappedPoint && !swappedSize && !swappedBackup && (size < poolSize) );

		// Save off old texturePoint:
		swappedPoint = allocPoint;
		swappedSize = size;

		// Reset texture pool to the beginning of the block:
		allocPoint = 0;

		// Save whatever's there now. The original code used Z:\texswap as
		// scratch storage; keeping it in memory avoids depending on a mounted
		// scratch volume during in-game Bink playback.
		swappedBackup = new unsigned char[size];
		assert( swappedBackup );
		if( swappedBackup )
			memcpy( swappedBackup, base, size );
	}

	void UnswapTextureMemory( void )
	{
		assert( swappedSize && swappedBackup );

		// Read back the data we saved before:
		if( swappedBackup )
		{
			memcpy( base, swappedBackup, swappedSize );
			delete [] swappedBackup;
			swappedBackup = NULL;
		}

		// Reset texture point
		allocPoint = swappedPoint;
		swappedPoint = 0;
		swappedSize = 0;
	}

	unsigned long Size( void )
	{
		return allocPoint;
	}

	unsigned long Capacity( void )
	{
		return poolSize;
	}

	unsigned long Available( void )
	{
		if ( !IsInitialized() || allocPoint >= poolSize )
			return 0;
		return poolSize - allocPoint;
	}

private:
	unsigned char	*base;
	unsigned long	allocPoint;
	unsigned long	poolSize;
	unsigned long	maxAlloc;

	// Extra bookkeeping for Bink texture nastiness:
	unsigned long swappedSize;
	unsigned long swappedPoint;
	unsigned char *swappedBackup;
};

// Global texture allocators:
extern StaticTextureAllocator	gTextures;

#endif
