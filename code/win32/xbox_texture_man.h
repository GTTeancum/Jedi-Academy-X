
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
	enum { MAX_FREE_BLOCKS = 4096 };

	StaticTextureAllocator( void )
	{
		base = NULL;
		allocPoint = 0;
		poolSize = 0;
		maxAlloc = 0;
		freeBlockCount = 0;
		swappedSize = 0;
		swappedPoint = 0;
		swappedFreeBlockCount = 0;
		swappedBackup = NULL;
	}

	void Initialize( unsigned long size )
	{
		// The renderer can be restarted without restarting the process. Keep
		// the contiguous pool alive across those restarts; allocating another
		// 20 MiB block would strand the old one and quickly exhaust an Xbox.
		if ( base && poolSize == size )
		{
			Reset();
			maxAlloc = 0;
			swappedSize = 0;
			swappedPoint = 0;
			swappedFreeBlockCount = 0;
			if ( swappedBackup )
			{
				delete [] swappedBackup;
				swappedBackup = NULL;
			}
			return;
		}

		Shutdown();
		base = (unsigned char *) D3D_AllocContiguousMemory( size, 0 );
		allocPoint = 0;
		poolSize = base ? size : 0;
		maxAlloc = 0;
		swappedSize = 0;
		swappedPoint = 0;
		swappedFreeBlockCount = 0;
		swappedBackup = NULL;
		Reset();
	}

	void Shutdown( void )
	{
		if ( swappedBackup )
		{
			delete [] swappedBackup;
			swappedBackup = NULL;
		}
		if ( base )
		{
			D3D_FreeContiguousMemory( base );
			base = NULL;
		}
		allocPoint = 0;
		poolSize = 0;
		maxAlloc = 0;
		freeBlockCount = 0;
		swappedSize = 0;
		swappedPoint = 0;
		swappedFreeBlockCount = 0;
	}

	// texNum remains unused; allocations are tracked by address and size.
	void *Allocate( unsigned long size, GLuint texNum )
	{
		unsigned long alignedSize;
		unsigned long i;

		if ( !base || !size )
			return NULL;

		if ( size > 0xffffffffUL - 127 )
			return NULL;
		alignedSize = (size + 127) & ~127;

		for ( i = 0; i < freeBlockCount; ++i )
		{
			if ( freeBlocks[i].size >= alignedSize )
			{
				const unsigned long offset = freeBlocks[i].offset;
				freeBlocks[i].offset += alignedSize;
				freeBlocks[i].size -= alignedSize;
				if ( freeBlocks[i].size == 0 )
				{
					RemoveFreeBlock(i);
				}

				allocPoint += alignedSize;

#ifndef FINAL_BUILD
				if( allocPoint > maxAlloc )
					maxAlloc = allocPoint;
#endif

				return base + offset;
			}
		}

		return NULL;
	}

	bool Free( void *memory, unsigned long size )
	{
		unsigned long alignedSize;
		unsigned long offset;
		unsigned long insertAt;

		if ( !memory || !size || !base )
			return false;
		if ( size > 0xffffffffUL - 127 )
			return false;

		alignedSize = (size + 127) & ~127;
		offset = (unsigned long)((unsigned char *)memory - base);
		if ( offset >= poolSize || alignedSize > poolSize - offset )
			return false;

		insertAt = 0;
		while ( insertAt < freeBlockCount &&
				freeBlocks[insertAt].offset < offset )
		{
			++insertAt;
		}

		if ( insertAt > 0 &&
			freeBlocks[insertAt - 1].offset + freeBlocks[insertAt - 1].size > offset )
			return false;
		if ( insertAt < freeBlockCount &&
			offset + alignedSize > freeBlocks[insertAt].offset )
			return false;

		if ( insertAt > 0 &&
			freeBlocks[insertAt - 1].offset + freeBlocks[insertAt - 1].size == offset )
		{
			freeBlocks[insertAt - 1].size += alignedSize;
			if ( insertAt < freeBlockCount &&
				freeBlocks[insertAt - 1].offset + freeBlocks[insertAt - 1].size ==
					freeBlocks[insertAt].offset )
			{
				freeBlocks[insertAt - 1].size += freeBlocks[insertAt].size;
				RemoveFreeBlock(insertAt);
			}
		}
		else if ( insertAt < freeBlockCount &&
			offset + alignedSize == freeBlocks[insertAt].offset )
		{
			freeBlocks[insertAt].offset = offset;
			freeBlocks[insertAt].size += alignedSize;
		}
		else
		{
			if ( freeBlockCount >= MAX_FREE_BLOCKS )
				return false;
			for ( unsigned long i = freeBlockCount; i > insertAt; --i )
			{
				freeBlocks[i] = freeBlocks[i - 1];
			}
			freeBlocks[insertAt].offset = offset;
			freeBlocks[insertAt].size = alignedSize;
			++freeBlockCount;
		}

		if ( alignedSize > allocPoint )
			allocPoint = 0;
		else
			allocPoint -= alignedSize;
		return true;
	}

	void Reset( void )
	{
		allocPoint = 0;
		freeBlockCount = 0;
		if ( base && poolSize )
		{
			freeBlocks[0].offset = 0;
			freeBlocks[0].size = poolSize;
			freeBlockCount = 1;
		}
	}

	// This is used by the bink code to make room for a giant texture
	// that doesn't need to live at the same time as any others
	void SwapTextureMemory( unsigned long size )
	{
		assert( !swappedPoint && !swappedSize && !swappedBackup && (size < poolSize) );

		// Save off old texturePoint:
		swappedPoint = allocPoint;
		swappedSize = size;
		swappedFreeBlockCount = freeBlockCount;
		memcpy(swappedFreeBlocks, freeBlocks,
			freeBlockCount * sizeof(freeBlocks[0]));

		// Reset texture pool to the beginning of the block:
		Reset();

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
		freeBlockCount = swappedFreeBlockCount;
		memcpy(freeBlocks, swappedFreeBlocks,
			freeBlockCount * sizeof(freeBlocks[0]));
		swappedPoint = 0;
		swappedSize = 0;
		swappedFreeBlockCount = 0;
	}

	unsigned long Size( void )
	{
		return allocPoint;
	}

	unsigned long Capacity( void )
	{
		return poolSize;
	}

	unsigned long Free( void )
	{
		if ( allocPoint > poolSize )
			return 0;
		return poolSize - allocPoint;
	}

	unsigned long LargestFree( void )
	{
		unsigned long largest = 0;
		for ( unsigned long i = 0; i < freeBlockCount; ++i )
		{
			if ( freeBlocks[i].size > largest )
				largest = freeBlocks[i].size;
		}
		return largest;
	}

private:
	struct FreeBlock
	{
		unsigned long offset;
		unsigned long size;
	};

	void RemoveFreeBlock( unsigned long index )
	{
		for ( unsigned long i = index + 1; i < freeBlockCount; ++i )
		{
			freeBlocks[i - 1] = freeBlocks[i];
		}
		--freeBlockCount;
	}

	unsigned char	*base;
	unsigned long	allocPoint;
	unsigned long	poolSize;
	unsigned long	maxAlloc;
	FreeBlock		freeBlocks[MAX_FREE_BLOCKS];
	unsigned long	freeBlockCount;

	// Extra bookkeeping for Bink texture nastiness:
	unsigned long swappedSize;
	unsigned long swappedPoint;
	FreeBlock swappedFreeBlocks[MAX_FREE_BLOCKS];
	unsigned long swappedFreeBlockCount;
	unsigned char *swappedBackup;
};

// Global texture allocators:
extern StaticTextureAllocator	gTextures;

#endif
