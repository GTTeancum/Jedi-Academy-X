/*
Copyright (C) 2000 Jack Palevich.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

// fakeglx.cpp - Uses Direct3D to implement a subset of OpenGL.

#include "stdio.h"
#ifdef _XBOX
#define _JKA_DDS_BRIDGE_INTERNAL_
#endif
#include "fakeglx.h"
#include "../xb_log.h"
#ifdef _XBOX
extern "C" void* JkaStaticTextureAlloc(unsigned long size, GLuint texNum);
#endif

// TODO: Fix this warning instead of disableing it
#pragma warning( disable : 4244 )
#pragma warning( disable : 4820 )
#pragma warning( disable : 4273 )

#define     D3D_OVERLOADS
#define     RELEASENULL(object) if (object) {object->Release();}

#include "xgraphics.h"

//#define PROFILE
#ifdef PROFILE
#pragma pack(push, 8)       // Make sure structure packing is set properly
#include <d3d8perf.h>
#pragma pack(pop)
#endif

// Some DX7 helper functions that we're still using with DX8
#ifdef D3DRGBA
#undef D3DRGBA
#endif
#define D3DRGBA                                 D3DCOLOR_COLORVALUE

#define D3DRGB(_r,_g,_b)                        D3DCOLOR_COLORVALUE(_r,_g,_b,1.f)

#define RGBA_MAKE                               D3DCOLOR_RGBA

#define TEXTURE0_SGIS							0x835E
#define TEXTURE1_SGIS							0x835F
#define D3D_TEXTURE_MAXANISOTROPY				0xf70001

#ifndef GL_POLYGON_OFFSET_FILL
#define GL_POLYGON_OFFSET_FILL					0x8037
#endif
#ifndef GL_CLIP_PLANE0
#define GL_CLIP_PLANE0							0x3000
#endif
#ifndef GL_ADD
#define GL_ADD									0x0104
#endif
#ifndef GL_EXP
#define GL_EXP									0x0800
#endif
#ifndef GL_EXP2
#define GL_EXP2									0x0801
#endif

void LocalDebugBreak()
{
	// Not needed atm
	//DebugBreak();
}

// Globals
bool g_force16bitTextures = true;
DWORD gWidth = 640;
DWORD gHeight = 480;
int bytes = 4;

//0 = interlaced 480i
//1 = progressive ("HD") 480p, 720p, depends on dashboard settings
int gVideoMode = 0;

class FakeGL;
static FakeGL* gFakeGL;

#ifdef _XBOX
extern "C" D3DTexture* WINAPI D3DDevice_CreateTexture2(DWORD Width, DWORD Height, DWORD Depth, DWORD Levels, DWORD Usage, D3DFORMAT Format, D3DRESOURCETYPE D3DType);
static DWORD g_fakeglTextureCount = 0;
static DWORD g_fakeglTextureFailures = 0;
static unsigned __int64 g_fakeglTextureBytes = 0;
#endif

class TextureEntry
{
public:
	TextureEntry()
	{
		m_id = 0;
		m_mipMap = 0;
		m_format = D3DFMT_UNKNOWN;
		m_internalFormat = 0;
#ifdef _XBOX
		m_ownsTextureHeader = false;
#endif

		m_glTexParameter2DMinFilter = GL_LINEAR_MIPMAP_LINEAR;	// Set up trilinear filtering
		m_glTexParameter2DMagFilter = GL_LINEAR;
		m_glTexParameter2DWrapS = GL_CLAMP;						// FakeGL 2009 sets it to WRAP -> CLAMP
		m_glTexParameter2DWrapT = GL_CLAMP;
		m_maxAnisotropy = 4.0;									// We also can bump up the anisotropy level to make things look nicer
	}
	~TextureEntry()
	{
	}

	void Release()
	{
#ifdef _XBOX
		if (m_mipMap && m_ownsTextureHeader)
		{
			delete (D3DTexture*)m_mipMap;
			m_mipMap = 0;
			m_ownsTextureHeader = false;
			return;
		}
#endif
		RELEASENULL(m_mipMap);
		m_mipMap = 0;
#ifdef _XBOX
		m_ownsTextureHeader = false;
#endif
	}

	GLuint m_id;
	IDirect3DTexture8* m_mipMap;
	D3DFORMAT m_format;
	GLint m_internalFormat;
#ifdef _XBOX
	bool m_ownsTextureHeader;
#endif

	GLint m_glTexParameter2DMinFilter;
	GLint m_glTexParameter2DMagFilter;
	GLint m_glTexParameter2DWrapS;
	GLint m_glTexParameter2DWrapT;
	float m_maxAnisotropy;
};


#define TASIZE 2000

class TextureTable 
{
public:
	TextureTable()
	{
		m_count = 0;
		m_size = 0;
		m_textures = 0;
		m_currentTexture = 0;
		m_currentID = 0;
		BindTexture(0);
	}
	~TextureTable()
	{
		DWORD i;
		for(i = 0; i < m_count; i++) 
		{
			m_textures[i].Release();

		}
		for(i = 0; i < TASIZE; i++) 
		{
			m_textureArray[i].Release();
		}

		delete [] m_textures;
	}

	void BindTexture(GLuint id)
	{
		TextureEntry* oldEntry = m_currentTexture;
		m_currentID = id;

		if ( id < TASIZE )
		{
			m_currentTexture = m_textureArray + id;
			if ( m_currentTexture->m_id )
			{
				return;
			}
		}
		else 
		{
			// Check overflow table.
			// Really ought to be a hash table.
			for(DWORD i = 0; i < m_count; i++)
			{
				if ( id == m_textures[i].m_id ) 
				{
					m_currentTexture =  m_textures + i;
					return;
				}
			}
			// It's a new ID.
			// Ensure space in the table
			if ( m_count >= m_size ) 
			{
				int newSize = m_size * 2 + 10;
				TextureEntry* newTextures = new TextureEntry[newSize];
				for(DWORD i = 0; i < m_count; i++ ) 
				{
					newTextures[i] = m_textures[i];
				}
				delete[] m_textures;
				m_textures = newTextures;
				m_size = newSize;
			}
			// Put new entry in table
			oldEntry = m_currentTexture;
			m_currentTexture = m_textures + m_count;
			m_count++;
		}
		if ( oldEntry ) 
		{
			*m_currentTexture = *oldEntry;
		}
		m_currentTexture->m_id = id;
		m_currentTexture->m_mipMap = NULL;		
	}

	int GetCurrentID() 
	{
		return m_currentID;
	}

	TextureEntry* GetCurrentEntry() 
	{
		return m_currentTexture;
	}

	TextureEntry* GetEntry(GLuint id)
	{
		if ( m_currentID == id && m_currentTexture ) 
		{
			return m_currentTexture;
		}
		if ( id < TASIZE ) 
		{
			return &m_textureArray[id];
		}
		else 
		{
			// Check overflow table.
			// Really ought to be a hash table.
			for(DWORD i = 0; i < m_count; i++)
			{
				if ( id == m_textures[i].m_id )
				{
					return  &m_textures[i];
				}
			}
		}
		return 0;
	}

	IDirect3DTexture8*  GetMipMap()
	{
		if ( m_currentTexture )
		{
			return m_currentTexture->m_mipMap;
		}
		return 0;
	}

	IDirect3DTexture8*  GetMipMap(int id)
	{
		TextureEntry* entry = GetEntry(id);
		if ( entry ) 
		{
			return entry->m_mipMap;
		}
		return 0;
	}

	D3DFORMAT GetSurfaceFormat()
	{
		if ( m_currentTexture ) 
		{
			return m_currentTexture->m_format;
		}
		return D3DFMT_UNKNOWN;
	}

	void SetTexture(IDirect3DTexture8* mipMap, D3DFORMAT d3dFormat, GLint internalFormat
#ifdef _XBOX
		, bool ownsTextureHeader = false
#endif
		)
	{
		if ( !m_currentTexture )
		{
			BindTexture(0);
		}
		m_currentTexture->Release();
		m_currentTexture->m_mipMap = mipMap;
		m_currentTexture->m_format = d3dFormat;
		m_currentTexture->m_internalFormat = internalFormat;
#ifdef _XBOX
		m_currentTexture->m_ownsTextureHeader = ownsTextureHeader;
#endif
	}

	void DeleteTexture(GLuint id)
	{
		TextureEntry* entry = GetEntry(id);
		if (!entry)
			return;
		entry->Release();
		entry->m_id = 0;
		entry->m_mipMap = NULL;
		entry->m_format = D3DFMT_UNKNOWN;
		entry->m_internalFormat = 0;
#ifdef _XBOX
		entry->m_ownsTextureHeader = false;
#endif
		if (m_currentID == id)
			BindTexture(0);
	}

	bool IsTexture(GLuint id)
	{
		if (!id)
			return false;
		TextureEntry* entry = GetEntry(id);
		return entry && entry->m_id == id;
	}

	GLint GetInternalFormat() 
	{
		if ( m_currentTexture ) 
		{
			return m_currentTexture->m_internalFormat;
		}
		return 0;
	}
private:
	GLuint m_currentID;
	DWORD m_count;
	DWORD m_size;
	TextureEntry m_textureArray[TASIZE];	// IDs 0..TASIZE-1
	TextureEntry* m_textures;				// Overflow

	TextureEntry* m_currentTexture;
};

#if 1
#define Clamp(x) (x) // No clamping -- we've made sure the inputs are in the range 0..1
#else
float Clamp(float x) 
{
	if ( x < 0 ) 
	{
		x = 0;
		LocalDebugBreak();
	}
	else if ( x > 1 ) 
	{
		x = 1;
		LocalDebugBreak();
	}
	return x;
}
#endif

static D3DBLEND GLToDXSBlend(GLenum glBlend)
{
	D3DBLEND result = D3DBLEND_ONE;
	switch ( glBlend ) 
	{
		case GL_ZERO: result = D3DBLEND_ZERO; break;
		case GL_ONE: result = D3DBLEND_ONE; break;
		case GL_DST_COLOR: result = D3DBLEND_DESTCOLOR; break;
		case GL_ONE_MINUS_DST_COLOR: result = D3DBLEND_INVDESTCOLOR; break;
		case GL_SRC_ALPHA: result = D3DBLEND_SRCALPHA; break;
		case GL_ONE_MINUS_SRC_ALPHA: result = D3DBLEND_INVSRCALPHA; break;
		case GL_DST_ALPHA: result = D3DBLEND_DESTALPHA; break;
		case GL_ONE_MINUS_DST_ALPHA: result = D3DBLEND_INVDESTALPHA; break;
		case GL_SRC_ALPHA_SATURATE: result = D3DBLEND_SRCALPHASAT; break;
		default: LocalDebugBreak(); break;
	}
	return result;
}

static D3DBLEND GLToDXDBlend(GLenum glBlend)
{
	D3DBLEND result = D3DBLEND_ONE;
	switch ( glBlend )
	{
		case GL_ZERO: result = D3DBLEND_ZERO; break;
		case GL_ONE: result = D3DBLEND_ONE; break;
		case GL_SRC_COLOR: result = D3DBLEND_SRCCOLOR; break;
		case GL_ONE_MINUS_SRC_COLOR: result = D3DBLEND_INVSRCCOLOR; break;
		case GL_SRC_ALPHA: result = D3DBLEND_SRCALPHA; break;
		case GL_ONE_MINUS_SRC_ALPHA: result = D3DBLEND_INVSRCALPHA; break;
		case GL_DST_ALPHA: result = D3DBLEND_DESTALPHA; break;
		case GL_ONE_MINUS_DST_ALPHA: result = D3DBLEND_INVDESTALPHA; break;
		default: LocalDebugBreak(); break;
	}
	return result;
}

static D3DCMPFUNC GLToDXCompare(GLenum func)
{
	D3DCMPFUNC result = D3DCMP_ALWAYS;
	switch ( func ) 
	{
		case GL_NEVER: result = D3DCMP_NEVER; break;
		case GL_LESS: result = D3DCMP_LESS; break;
		case GL_EQUAL: result = D3DCMP_EQUAL; break;
		case GL_LEQUAL: result = D3DCMP_LESSEQUAL; break;
		case GL_GREATER: result = D3DCMP_GREATER; break;
		case GL_NOTEQUAL: result = D3DCMP_NOTEQUAL; break;
		case GL_GEQUAL: result = D3DCMP_GREATEREQUAL; break;
		case GL_ALWAYS: result = D3DCMP_ALWAYS; break;
		default: break;
	}
	return result;
}

static D3DFOGMODE GLToDXFogMode(GLint mode)
{
	switch ( mode )
	{
	case GL_LINEAR:
		return D3DFOG_LINEAR;
	case GL_EXP:
		return D3DFOG_EXP;
	case GL_EXP2:
		return D3DFOG_EXP2;
	default:
		break;
	}
	return D3DFOG_NONE;
}

/*
   OpenGL                      MinFilter           MipFilter       Comments
   GL_NEAREST                  D3DTFN_POINT        D3DTFP_NONE
   GL_LINEAR                   D3DTFN_LINEAR       D3DTFP_NONE
   GL_NEAREST_MIPMAP_NEAREST   D3DTFN_POINT        D3DTFP_POINT        
   GL_LINEAR_MIPMAP_NEAREST    D3DTFN_LINEAR       D3DTFP_POINT    bilinear
   GL_NEAREST_MIPMAP_LINEAR    D3DTFN_POINT        D3DTFP_LINEAR 
   GL_LINEAR_MIPMAP_LINEAR     D3DTFN_LINEAR       D3DTFP_LINEAR   trilinear
*/

static D3DTEXTUREFILTERTYPE GLToDXMinFilter(GLint filter)
{
	D3DTEXTUREFILTERTYPE result = D3DTEXF_LINEAR;
	switch ( filter ) 
	{
		case GL_NEAREST: result = D3DTEXF_POINT; break;
		case GL_LINEAR: result = D3DTEXF_LINEAR; break;
		case GL_NEAREST_MIPMAP_NEAREST: result = D3DTEXF_POINT; break;
		case GL_LINEAR_MIPMAP_NEAREST: result = D3DTEXF_LINEAR; break;
		case GL_NEAREST_MIPMAP_LINEAR: result = D3DTEXF_POINT; break;
		case GL_LINEAR_MIPMAP_LINEAR: result = D3DTEXF_LINEAR; break;
	default:
		LocalDebugBreak();
		break;
	}
	return result;
}

static D3DTEXTUREFILTERTYPE GLToDXMipFilter(GLint filter)
{
	D3DTEXTUREFILTERTYPE result = D3DTEXF_LINEAR;
	switch ( filter ) 
	{
		case GL_NEAREST: result = D3DTEXF_NONE; break;
		case GL_LINEAR: result = D3DTEXF_NONE; break;
		case GL_NEAREST_MIPMAP_NEAREST: result = D3DTEXF_POINT; break;
		case GL_LINEAR_MIPMAP_NEAREST: result = D3DTEXF_POINT; break;
		case GL_NEAREST_MIPMAP_LINEAR: result = D3DTEXF_LINEAR; break;
		case GL_LINEAR_MIPMAP_LINEAR: result = D3DTEXF_LINEAR; break;
	default:
		LocalDebugBreak();
		break;
	}
	return result;
}

static D3DTEXTUREFILTERTYPE GLToDXMagFilter(GLint filter)
{
	D3DTEXTUREFILTERTYPE result = D3DTEXF_POINT;
	switch ( filter )
	{
		case GL_NEAREST: result = D3DTEXF_POINT; break;
		case GL_LINEAR: result = D3DTEXF_LINEAR; break;
	default:
		LocalDebugBreak();
		break;
	}
	return result;
}

static D3DTEXTUREADDRESS GLToDXTextureAddress(GLint wrap)
{
	D3DTEXTUREADDRESS result = D3DTADDRESS_CLAMP;
	switch ( wrap )
	{
		case GL_REPEAT:
			result = D3DTADDRESS_WRAP;
			break;
		case GL_CLAMP:
#ifdef GL_CLAMP_TO_EDGE
		case GL_CLAMP_TO_EDGE:
#endif
			result = D3DTADDRESS_CLAMP;
			break;
		default:
			LocalDebugBreak();
			break;
	}
	return result;
}

static D3DTEXTUREOP GLToDXTextEnvMode(GLint mode)
{
	D3DTEXTUREOP result = D3DTOP_MODULATE;
	switch ( mode ) 
	{
		case GL_MODULATE: result = D3DTOP_MODULATE; break;
		case GL_DECAL: result = D3DTOP_SELECTARG1; break; // Fix this
		case GL_BLEND: result = D3DTOP_BLENDTEXTUREALPHA; break;
		case GL_REPLACE: result = D3DTOP_SELECTARG1; break;
		case GL_ADD: result = D3DTOP_ADD; break;
		default: break;
	}
	return result;
}

#define MAXSTATES 8

class TextureStageState 
{
public:
	TextureStageState()
	{
		m_currentTexture = 0;
		m_glTextEnvMode = GL_MODULATE;
		m_glTexture2D = false;
		m_dirty = true;
	}

	bool GetDirty()
	{
		return m_dirty;
	}

	void SetDirty(bool dirty) 
	{ 
		m_dirty = dirty;
	}

	void DirtyTexture(GLuint textureID)
	{
		if ( textureID == m_currentTexture ) 
		{
			m_dirty = true;
		}
	}

	GLuint GetCurrentTexture() { return m_currentTexture; }
	void SetCurrentTexture(GLuint texture) { m_dirty = true; m_currentTexture = texture; }

	GLfloat GetTextEnvMode() { return m_glTextEnvMode; }
	void SetTextEnvMode(GLfloat mode) { m_dirty = true; m_glTextEnvMode = mode; }

	bool GetTexture2D() { return m_glTexture2D; }
	void SetTexture2D(bool texture2D) { m_dirty = true; m_glTexture2D = texture2D; }

private:
	
	GLuint m_currentTexture;
	GLfloat m_glTextEnvMode;
	bool m_glTexture2D;
	bool m_dirty;
};

class TextureState
{
public:
	TextureState()
	{
		m_currentStage = 0;
		memset(&m_stage, 0, sizeof(m_stage));
		m_dirty = false;
		m_mainBlend = false;
	}

	void SetMaxStages(int maxStages)
	{
		m_maxStages = maxStages;
		for(int i = 0; i < m_maxStages;i++)
		{
			m_stage[i].SetDirty(true);
		}
		m_dirty = true;
	}

	// Keep track of changes to texture stage state
	void SetCurrentStage(int index)
	{
		m_currentStage = index;
	}

	int GetMaxStages() { return m_maxStages; }
	int GetCurrentStage() { return m_currentStage; }
	GLuint GetStageTexture(int index) { return (index >= 0 && index < MAXSTATES) ? m_stage[index].GetCurrentTexture() : 0; }
	bool GetStageTexture2D(int index) { return (index >= 0 && index < MAXSTATES) ? m_stage[index].GetTexture2D() : false; }
	bool GetStageDirty(int index) { return (index >= 0 && index < MAXSTATES) ? m_stage[index].GetDirty() : false; }
	GLfloat GetStageTextEnvMode(int index) { return (index >= 0 && index < MAXSTATES) ? m_stage[index].GetTextEnvMode() : 0.0f; }

	bool GetDirty() { return m_dirty; }

	void DirtyTexture(int textureID)
	{
		for(int i = 0; i < m_maxStages;i++)
		{
			m_stage[i].DirtyTexture(textureID);
		}
		m_dirty = true;
	}

	void SetMainBlend(bool mainBlend)
	{
		m_mainBlend = mainBlend;
		m_stage[0].SetDirty(true);
		m_dirty = true;
	}
	
	// These methods apply to the current stage

	GLuint GetCurrentTexture() { return Get()->GetCurrentTexture(); }

	void SetCurrentTexture(GLuint texture) { m_dirty = true; Get()->SetCurrentTexture(texture); }

	GLfloat GetTextEnvMode() { return Get()->GetTextEnvMode(); }

	void SetTextEnvMode(GLfloat mode) { m_dirty = true; Get()->SetTextEnvMode(mode); }
	
	bool GetTexture2D() { return Get()->GetTexture2D(); }

	void SetTexture2D(bool texture2D) { m_dirty = true; Get()->SetTexture2D(texture2D); }

	void ForceStageDirty(int index)
	{
		if (index >= 0 && index < m_maxStages)
		{
			m_stage[index].SetDirty(true);
			m_dirty = true;
		}
	}

	void SetTextureStageState(IDirect3DDevice8* pD3DDev, TextureTable* textures)
	{
#ifdef _XBOX
		{
			static int s_xboxTextureStateEntryStage1LogCount = 0;
			if (m_stage[1].GetTexture2D() && s_xboxTextureStateEntryStage1LogCount < 8)
			{
				XBLF("JA: fakegl texture state entry dirty=%d maxStages=%d currentStage=%d stage0 dirty=%d tex=%u enabled=%d env=0x%08x stage1 dirty=%d tex=%u enabled=%d env=0x%08x",
					m_dirty ? 1 : 0,
					m_maxStages,
					m_currentStage,
					m_stage[0].GetDirty() ? 1 : 0,
					(unsigned int)m_stage[0].GetCurrentTexture(),
					m_stage[0].GetTexture2D() ? 1 : 0,
					(unsigned int)m_stage[0].GetTextEnvMode(),
					m_stage[1].GetDirty() ? 1 : 0,
					(unsigned int)m_stage[1].GetCurrentTexture(),
					m_stage[1].GetTexture2D() ? 1 : 0,
					(unsigned int)m_stage[1].GetTextEnvMode());
				++s_xboxTextureStateEntryStage1LogCount;
			}
		}
#endif
		if ( ! m_dirty )
		{
#ifdef _XBOX
			static int s_xboxTextureStateCleanLogCount = 0;
			if (s_xboxTextureStateCleanLogCount < 4)
			{
				XBLF("JA: fakegl texture state apply skipped dirty=0 currentStage=%d maxStages=%d stage0 tex=%u enabled=%d stage1 tex=%u enabled=%d",
					m_currentStage,
					m_maxStages,
					(unsigned int)m_stage[0].GetCurrentTexture(),
					m_stage[0].GetTexture2D() ? 1 : 0,
					(unsigned int)m_stage[1].GetCurrentTexture(),
					m_stage[1].GetTexture2D() ? 1 : 0);
				++s_xboxTextureStateCleanLogCount;
			}
#endif
			return;
		}
		static bool firstTime = true;
		if ( firstTime ) 
		{
			firstTime = false;
			for(int i = 0; i < m_maxStages; i++ ) 
			{
				pD3DDev->SetTextureStageState(i, D3DTSS_TEXCOORDINDEX, i);
				pD3DDev->SetTextureStageState(i, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
			}
		}

		m_dirty = false;

		for(int i = 0; i < m_maxStages; i++ )
		{
#ifdef _XBOX
			{
				static int s_xboxTextureStageLoopLogCount = 0;
				if ((i == 1 || s_xboxTextureStageLoopLogCount < 4) && s_xboxTextureStageLoopLogCount < 16)
				{
					XBLF("JA: fakegl texture stage loop i=%d maxStages=%d dirty=%d tex=%u enabled=%d env=0x%08x textureDirty=%d currentStage=%d",
						i,
						m_maxStages,
						m_stage[i].GetDirty() ? 1 : 0,
						(unsigned int)m_stage[i].GetCurrentTexture(),
						m_stage[i].GetTexture2D() ? 1 : 0,
						(unsigned int)m_stage[i].GetTextEnvMode(),
						m_dirty ? 1 : 0,
						m_currentStage);
					++s_xboxTextureStageLoopLogCount;
				}
			}
#endif
			if ( ! m_stage[i].GetDirty() ) 
			{
#ifdef _XBOX
				static int s_xboxStageCleanLogCount = 0;
				if (i == 1 && s_xboxStageCleanLogCount < 8)
				{
					XBLF("JA: fakegl stage state stage=1 skip dirty=0 tex=%u enabled=%d env=0x%08x currentStage=%d",
						(unsigned int)m_stage[i].GetCurrentTexture(),
						m_stage[i].GetTexture2D() ? 1 : 0,
						(unsigned int)m_stage[i].GetTextEnvMode(),
						m_currentStage);
					++s_xboxStageCleanLogCount;
				}
#endif
				continue;
			}
			m_stage[i].SetDirty(false);

			if ( m_stage[i].GetTexture2D() ) 
			{
				DWORD color1 = D3DTA_TEXTURE;
				int textEnvMode =  m_stage[i].GetTextEnvMode();
				DWORD colorOp = GLToDXTextEnvMode(textEnvMode);
				if ( i > 0 && textEnvMode == GL_BLEND ) 
				{
					// Assume we're doing multi-texture light mapping.
					// I don't think this is the right way to do this
					// but it works for D3DQuake.
					colorOp = D3DTOP_MODULATE;
					color1 |= D3DTA_COMPLEMENT;
				}
#ifdef _XBOX
				if (i == 1)
				{
					static int s_xboxStage1PreApplyLogCount = 0;
					if (s_xboxStage1PreApplyLogCount < 8)
					{
						XBLF("JA: fakegl stage1 preapply tex=%u env=0x%08x colorOp=0x%08lx colorArg1=0x%08lx colorArg2=0x%08lx",
							(unsigned int)m_stage[i].GetCurrentTexture(),
							(unsigned int)textEnvMode,
							(unsigned long)colorOp,
							(unsigned long)color1,
							(unsigned long)D3DTA_CURRENT);
						++s_xboxStage1PreApplyLogCount;
					}
				}
#endif
				HRESULT hrColorArg1 = pD3DDev->SetTextureStageState( i, D3DTSS_COLORARG1, color1);
				HRESULT hrColorArg2 = pD3DDev->SetTextureStageState( i, D3DTSS_COLORARG2, D3DTA_CURRENT);
				HRESULT hrColorOp = pD3DDev->SetTextureStageState( i, D3DTSS_COLOROP, colorOp);
				HRESULT hrTexCoordIndex = pD3DDev->SetTextureStageState( i, D3DTSS_TEXCOORDINDEX, i);
				HRESULT hrTextureTransform = pD3DDev->SetTextureStageState( i, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
				pD3DDev->SetTextureStageState( i, D3DTSS_MAXMIPLEVEL, 0 );
				pD3DDev->SetTextureStageState( i, D3DTSS_MIPMAPLODBIAS, 0 );
#ifdef _XBOX
				pD3DDev->SetTextureStageState( i, D3DTSS_COLORKEYOP, D3DTCOLORKEYOP_DISABLE );
				pD3DDev->SetTextureStageState( i, D3DTSS_COLORSIGN, 0 );
				pD3DDev->SetTextureStageState( i, D3DTSS_ALPHAKILL, D3DTALPHAKILL_DISABLE );
#endif
				DWORD alpha1 = D3DTA_TEXTURE;
				DWORD alpha2 = D3DTA_CURRENT;
				DWORD alphaOp;
				alphaOp = GLToDXTextEnvMode(textEnvMode);
				if (i == 0 && m_mainBlend )
				{
					alphaOp = D3DTOP_MODULATE;	// Otherwise the console is never transparent
				}
				HRESULT hrAlphaArg1 = pD3DDev->SetTextureStageState( i, D3DTSS_ALPHAARG1, alpha1);
				HRESULT hrAlphaArg2 = pD3DDev->SetTextureStageState( i, D3DTSS_ALPHAARG2, alpha2);
				HRESULT hrAlphaOp = pD3DDev->SetTextureStageState( i, D3DTSS_ALPHAOP,   alphaOp);
#ifdef _XBOX
				{
					static int s_xboxStageStateLogCount = 0;
					static int s_xboxStage1StateLogCount = 0;
					if (s_xboxStageStateLogCount < 8 || (i > 0 && s_xboxStage1StateLogCount < 8))
					{
						XBLF("JA: fakegl stage state stage=%d enabled=1 tex=%u env=0x%08x colorOp=0x%08lx colorArg1=0x%08lx colorArg2=0x%08lx alphaOp=0x%08lx alphaArg1=0x%08lx alphaArg2=0x%08lx",
							i,
							(unsigned int)m_stage[i].GetCurrentTexture(),
							(unsigned int)textEnvMode,
							(unsigned long)colorOp,
							(unsigned long)color1,
							(unsigned long)D3DTA_CURRENT,
							(unsigned long)alphaOp,
							(unsigned long)alpha1,
							(unsigned long)alpha2);
						++s_xboxStageStateLogCount;
						if (i > 0)
						{
							++s_xboxStage1StateLogCount;
						}
					}
				}
				if (i == 1)
				{
					static int s_xboxStage1ApplyHrLogCount = 0;
					if (s_xboxStage1ApplyHrLogCount < 8)
					{
						XBLF("JA: fakegl stage1 apply hr colorArg1=0x%08lx colorArg2=0x%08lx colorOp=0x%08lx alphaArg1=0x%08lx alphaArg2=0x%08lx alphaOp=0x%08lx",
							(unsigned long)hrColorArg1,
							(unsigned long)hrColorArg2,
							(unsigned long)hrColorOp,
							(unsigned long)hrAlphaArg1,
							(unsigned long)hrAlphaArg2,
							(unsigned long)hrAlphaOp);
						++s_xboxStage1ApplyHrLogCount;
					}
				}
				if (i == 1)
				{
					static int s_xboxStage1TexTransformLogCount = 0;
					if (s_xboxStage1TexTransformLogCount < 8)
					{
						XBLF("JA: fakegl stage1 texcoord state hr index=0x%08lx transform=0x%08lx flags=COUNT2",
							(unsigned long)hrTexCoordIndex,
							(unsigned long)hrTextureTransform);
						++s_xboxStage1TexTransformLogCount;
					}
				}
#endif

				TextureEntry* entry = textures->GetEntry(m_stage[i].GetCurrentTexture());
				if ( entry ) 
				{
#ifdef _XBOX
					if (i == 1)
					{
						static int s_xboxStage1EntryLogCount = 0;
						if (s_xboxStage1EntryLogCount < 8)
						{
							XBLF("JA: fakegl stage1 texture entry reqTex=%u entryId=%u ptr=%p fmt=0x%08x internal=0x%08x min=%d mag=%d",
								(unsigned int)m_stage[i].GetCurrentTexture(),
								(unsigned int)entry->m_id,
								(void*)entry->m_mipMap,
								(unsigned int)entry->m_format,
								(unsigned int)entry->m_internalFormat,
								entry->m_glTexParameter2DMinFilter,
								entry->m_glTexParameter2DMagFilter);
							++s_xboxStage1EntryLogCount;
						}
					}
#endif
					int minFilter = entry->m_glTexParameter2DMinFilter;
					DWORD dxMinFilter = GLToDXMinFilter(minFilter);
					DWORD dxMipFilter = GLToDXMipFilter(minFilter);
					DWORD dxMagFilter = GLToDXMagFilter(entry->m_glTexParameter2DMagFilter);
					DWORD dxAddressU = GLToDXTextureAddress(entry->m_glTexParameter2DWrapS);
					DWORD dxAddressV = GLToDXTextureAddress(entry->m_glTexParameter2DWrapT);

					// Avoid setting anisotropic if the user doesn't request it.
					static bool bSetMaxAnisotropy = false;
					if ( entry->m_maxAnisotropy != 1.0f ) 
					{
						bSetMaxAnisotropy = true;
						if ( dxMagFilter == D3DTEXF_LINEAR) 
						{
							dxMagFilter = D3DTEXF_ANISOTROPIC;
						}
						if ( dxMinFilter == D3DTEXF_LINEAR) 
						{
							dxMinFilter = D3DTEXF_ANISOTROPIC;
						}
					}
					if ( bSetMaxAnisotropy ) 
					{
						pD3DDev->SetTextureStageState( i, D3DTSS_MAXANISOTROPY, entry->m_maxAnisotropy);
					}
					pD3DDev->SetTextureStageState( i, D3DTSS_MINFILTER, dxMinFilter );
					pD3DDev->SetTextureStageState( i, D3DTSS_MIPFILTER, dxMipFilter );
					pD3DDev->SetTextureStageState( i, D3DTSS_MAGFILTER,  dxMagFilter);
					pD3DDev->SetTextureStageState( i, D3DTSS_ADDRESSU, dxAddressU );
					pD3DDev->SetTextureStageState( i, D3DTSS_ADDRESSV, dxAddressV );
#ifdef _XBOX
					if (i == 1)
					{
						static int s_xboxStage1AddressLogCount = 0;
						if (s_xboxStage1AddressLogCount < 8)
						{
							XBLF("JA: fakegl stage1 address wrapS=0x%08x wrapT=0x%08x addrU=0x%08lx addrV=0x%08lx",
								(unsigned int)entry->m_glTexParameter2DWrapS,
								(unsigned int)entry->m_glTexParameter2DWrapT,
								(unsigned long)dxAddressU,
								(unsigned long)dxAddressV);
							++s_xboxStage1AddressLogCount;
						}
					}
#endif
					IDirect3DTexture8* pTexture = entry->m_mipMap;
					// char buf[100];
					// sprintf(buf,"SetTexture 0x%08x\n", pTexture);
					// OutputDebugString(buf);
					if ( pTexture )
					{
#ifdef _XBOX
						static int s_xboxSetTextureLogCount = 0;
						static int s_xboxSetTextureStage1LogCount = 0;
						const bool logSetTexture = (s_xboxSetTextureLogCount < 8 || (i > 0 && s_xboxSetTextureStage1LogCount < 8));
						if (i == 1)
						{
							static int s_xboxStage1SetTextureDirectLogCount = 0;
							if (s_xboxStage1SetTextureDirectLogCount < 8)
							{
								XBLF("JA: fakegl stage1 SetTexture direct pre texid=%d ptr=%p fmt=0x%08x internal=0x%08x",
									entry->m_id, (void*)pTexture, (unsigned int)entry->m_format,
									(unsigned int)entry->m_internalFormat);
								++s_xboxStage1SetTextureDirectLogCount;
							}
						}
						if (logSetTexture)
						{
							XBLF("JA: fakegl SetTexture stage=%d texid=%d ptr=%p fmt=0x%08x internal=0x%08x",
								i, entry->m_id, (void*)pTexture, (unsigned int)entry->m_format,
								(unsigned int)entry->m_internalFormat);
						}
#endif
						HRESULT hrSetTexture = pD3DDev->SetTexture( i, pTexture);
#ifdef _XBOX
						if (i == 1)
						{
							static int s_xboxStage1SetTextureDirectHrLogCount = 0;
							if (s_xboxStage1SetTextureDirectHrLogCount < 8)
							{
								XBLF("JA: fakegl stage1 SetTexture direct post hr=0x%08lx",
									(unsigned long)hrSetTexture);
								++s_xboxStage1SetTextureDirectHrLogCount;
							}
						}
						if (logSetTexture)
						{
							XBLF("JA: fakegl SetTexture stage=%d hr=0x%08lx", i,
								(unsigned long)hrSetTexture);
							++s_xboxSetTextureLogCount;
							if (i > 0)
							{
								++s_xboxSetTextureStage1LogCount;
							}
						}
#endif
					}
					else
					{
#ifdef _XBOX
						if (i == 1)
						{
							static int s_xboxStage1NullTextureLogCount = 0;
							if (s_xboxStage1NullTextureLogCount < 32)
							{
								XBLF("JA: fakegl stage1 texture entry missing mip reqTex=%u entryId=%u",
									(unsigned int)m_stage[i].GetCurrentTexture(),
									(unsigned int)entry->m_id);
								++s_xboxStage1NullTextureLogCount;
							}
						}
#endif
						LocalDebugBreak();
					}
				}
				else
				{
#ifdef _XBOX
					if (i == 1)
					{
						static int s_xboxStage1MissingEntryLogCount = 0;
						if (s_xboxStage1MissingEntryLogCount < 32)
						{
							XBLF("JA: fakegl stage1 texture entry missing reqTex=%u",
								(unsigned int)m_stage[i].GetCurrentTexture());
							++s_xboxStage1MissingEntryLogCount;
						}
					}
#endif
				}
			}
			else 
			{
				pD3DDev->SetTexture( i, NULL);
				pD3DDev->SetTextureStageState( i, D3DTSS_COLORARG1, D3DTA_TEXTURE);
				pD3DDev->SetTextureStageState( i, D3DTSS_COLORARG2, D3DTA_CURRENT);
				pD3DDev->SetTextureStageState( i, D3DTSS_COLOROP, D3DTOP_DISABLE);
				pD3DDev->SetTextureStageState( i, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
				pD3DDev->SetTextureStageState( i, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
				pD3DDev->SetTextureStageState( i, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
				pD3DDev->SetTextureStageState( i, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
				pD3DDev->SetTextureStageState( i, D3DTSS_MAXMIPLEVEL, 0 );
				pD3DDev->SetTextureStageState( i, D3DTSS_MIPMAPLODBIAS, 0 );
#ifdef _XBOX
				pD3DDev->SetTextureStageState( i, D3DTSS_COLORKEYOP, D3DTCOLORKEYOP_DISABLE );
				pD3DDev->SetTextureStageState( i, D3DTSS_COLORSIGN, 0 );
				pD3DDev->SetTextureStageState( i, D3DTSS_ALPHAKILL, D3DTALPHAKILL_DISABLE );
#endif
#ifdef _XBOX
				{
					static int s_xboxStageDisableLogCount = 0;
					if (s_xboxStageDisableLogCount < 8)
					{
						XBLF("JA: fakegl stage state stage=%d enabled=0 tex=%u colorOp=DISABLE",
							i,
							(unsigned int)m_stage[i].GetCurrentTexture());
						++s_xboxStageDisableLogCount;
					}
				}
#endif
			}
		}
	}

private:
	TextureStageState* Get() 
	{
		return m_stage + m_currentStage;
	}

	bool m_dirty;
	bool m_mainBlend;
	int m_maxStages;
	int m_currentStage;
	TextureStageState m_stage[MAXSTATES];
};

// This class buffers up all the glVertex calls between
// glBegin and glEnd.

// USE_DRAWINDEXEDPRIMITIVE seems slightly faster (54 fps vs 53 fps) than USE_DRAWPRIMITIVE.
// USE_DRAWINDEXEDPRIMITIVEVB is much slower (30fps vs 54fps), at least on GeForce Win9x 3.75.

// DrawPrimitive works for DX8, the other ones don't work right yet.

#define USE_DRAWPRIMITIVE

#ifdef USE_DRAWPRIMITIVE
class OGLPrimitiveVertexBuffer 
{
public:
	OGLPrimitiveVertexBuffer()
	{
		m_drawMode = -1;
		m_size = 0;
		m_count = 0;
		m_OGLPrimitiveVertexBuffer = 0;
		m_vertexCount = 0;
		m_vertexTypeDesc = 0;
		memset(m_textureCoords, 0, sizeof(m_textureCoords));

		m_pD3DDev = 0;
		m_color = 0xff000000; // Don't know if this is correct
	}

	~OGLPrimitiveVertexBuffer()
	{
#ifdef USE_PUSHBUFFER
		RELEASENULL(m_pushBuffer);
#endif
		delete [] m_OGLPrimitiveVertexBuffer;
	}

	HRESULT Initialize(IDirect3DDevice8* pD3DDev, IDirect3D8* pD3D, bool hardwareTandL, DWORD typeDesc)
	{
		m_pD3DDev = pD3DDev;
		if (m_vertexTypeDesc != typeDesc) 
		{
			m_vertexTypeDesc = typeDesc;
			m_vertexSize = 0;
			if ( m_vertexTypeDesc & D3DFVF_XYZ ) 
			{
				m_vertexSize += 3 * sizeof(float);
			}
			if ( m_vertexTypeDesc & D3DFVF_DIFFUSE )
			{
				m_vertexSize += 4;
			}
			int textureStages = (m_vertexTypeDesc & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
			m_vertexSize += 2 * sizeof(float) * textureStages;
		}

#ifdef USE_PUSHBUFFER
		UINT pbSize = 384*1024; //Only used if RunUsingCpuCopy == false, it's overriden by SetPushBufferSize(), so right now it just sits here for reference
		HRESULT hr;
		hr = pD3DDev->CreatePushBuffer(pbSize, true, &m_pushBuffer);
		if ( FAILED(hr) ) 
			return hr;
#endif
		return S_OK;
	}

	DWORD GetVertexTypeDesc()
	{
		return m_vertexTypeDesc;
	}

	bool HasDevice() const
	{
		return m_pD3DDev != 0;
	}

	void EnsureDevice(IDirect3DDevice8* pD3DDev)
	{
		if (pD3DDev && m_pD3DDev != pD3DDev)
		{
#ifdef _XBOX
			static int s_xboxEnsureDeviceLogCount = 0;
			if (s_xboxEnsureDeviceLogCount < 16)
			{
				XBLF("JA: fakegl primitive vb device refresh old=%p new=%p\n",
					(void*)m_pD3DDev, (void*)pD3DDev);
			}
			s_xboxEnsureDeviceLogCount++;
#endif
			m_pD3DDev = pD3DDev;
		}
	}

	LPVOID GetOGLPrimitiveVertexBuffer()
	{
		return m_OGLPrimitiveVertexBuffer;
	}

	DWORD GetVertexCount()
	{
		return m_vertexCount;
	}

	inline void SetColor(D3DCOLOR color)
	{
		m_color = color;
	}
	
	inline void SetTextureCoord0(float u, float v)
	{
		DWORD* pCoords = (DWORD*) m_textureCoords;
		pCoords[0] = *(DWORD*)& u;
		pCoords[1] = *(DWORD*)& v;
	}

	inline void SetTextureCoord(int textStage, float u, float v)
	{
		DWORD* pCoords = (DWORD*) m_textureCoords + (textStage << 1);
		pCoords[0] = *(DWORD*)& u;
		pCoords[1] = *(DWORD*)& v;
	}

	inline void SetVertex(float x, float y, float z)
	{
		int newCount = m_count + m_vertexSize;
		if (newCount > m_size) {
			Ensure(m_vertexSize);
		}
		DWORD* pFloat = (DWORD*) (m_OGLPrimitiveVertexBuffer + m_count);
		pFloat[0] = *(DWORD*)& x;
		pFloat[1] = *(DWORD*)& y;
		pFloat[2] = *(DWORD*)& z;
		const DWORD* pCoords = (DWORD*) m_textureCoords;
		switch(m_vertexTypeDesc){
		case (D3DFVF_XYZ | D3DFVF_DIFFUSE | (1 << D3DFVF_TEXCOUNT_SHIFT)):
			pFloat[3] = m_color;
			pFloat[4] = pCoords[0];
			pFloat[5] = pCoords[1];
			break;
		case (D3DFVF_XYZ | D3DFVF_DIFFUSE | (2 << D3DFVF_TEXCOUNT_SHIFT)):
			pFloat[3] = m_color;
			pFloat[4] = pCoords[0];
			pFloat[5] = pCoords[1];
			pFloat[6] = pCoords[2];
			pFloat[7] = pCoords[3];
			break;
		default:
			{
				pFloat += 3;
				if ( m_vertexTypeDesc & D3DFVF_DIFFUSE ) 
				{
					*pFloat++ = m_color;
				}
				int textureStages = (m_vertexTypeDesc & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
				for ( int i = 0; i < textureStages; i++ )
				{
					*pFloat++ = *pCoords++;
					*pFloat++ = *pCoords++;
				}
			}
			break;
		}
		m_count = newCount;
		m_vertexCount++;

		// TO DO: Flush vertex buffer if larger than 1000 vertexes.
		// Have to do this modulo vertexes-per-primitive
	}

#ifdef _XBOX
	bool ClipTriangleListAgainstPlane(const float plane[4], const D3DMATRIX *modelView)
	{
		if (!plane || !modelView || m_vertexTypeDesc == 0 || m_vertexSize <= 0)
		{
			return false;
		}
		if (m_vertexCount < 3 || (m_vertexCount % 3) != 0)
		{
			return false;
		}

		DWORD dstVertex = 0;
		DWORD removedTris = 0;
		const DWORD srcVertexCount = m_vertexCount;

		for (DWORD srcVertex = 0; srcVertex < srcVertexCount; srcVertex += 3)
		{
			bool outside[3];
			for (int i = 0; i < 3; ++i)
			{
				const float *v = (const float *)(m_OGLPrimitiveVertexBuffer + ((srcVertex + i) * m_vertexSize));
				const float x = v[0];
				const float y = v[1];
				const float z = v[2];
				const float tx = x * modelView->_11 + y * modelView->_21 + z * modelView->_31 + modelView->_41;
				const float ty = x * modelView->_12 + y * modelView->_22 + z * modelView->_32 + modelView->_42;
				const float tz = x * modelView->_13 + y * modelView->_23 + z * modelView->_33 + modelView->_43;
				const float dist = tx * plane[0] + ty * plane[1] + tz * plane[2] + plane[3];
				outside[i] = (dist < 0.0f);
			}

			if (outside[0] && outside[1] && outside[2])
			{
				++removedTris;
				continue;
			}

			if (dstVertex != srcVertex)
			{
				memmove(m_OGLPrimitiveVertexBuffer + (dstVertex * m_vertexSize),
					m_OGLPrimitiveVertexBuffer + (srcVertex * m_vertexSize),
					3 * m_vertexSize);
			}
			dstVertex += 3;
		}

		if (removedTris)
		{
			static int s_clipTriangleLogBudget = 32;
			if (s_clipTriangleLogBudget > 0)
			{
				XBLF("JA: fakegl CPU clip trianglelist removed=%lu kept=%lu plane=%g,%g,%g,%g",
					(unsigned long)removedTris,
					(unsigned long)(dstVertex / 3),
					plane[0], plane[1], plane[2], plane[3]);
				--s_clipTriangleLogBudget;
			}
			m_vertexCount = dstVertex;
			m_count = (int)(dstVertex * m_vertexSize);
			return true;
		}

		return false;
	}

	bool IsEntireDrawOutsidePlane(const float plane[4], const D3DMATRIX *modelView) const
	{
		if (!plane || !modelView || m_vertexTypeDesc == 0 || m_vertexSize <= 0 || m_vertexCount == 0)
		{
			return false;
		}

		for (DWORD i = 0; i < m_vertexCount; ++i)
		{
			const float *v = (const float *)(m_OGLPrimitiveVertexBuffer + (i * m_vertexSize));
			const float x = v[0];
			const float y = v[1];
			const float z = v[2];
			const float tx = x * modelView->_11 + y * modelView->_21 + z * modelView->_31 + modelView->_41;
			const float ty = x * modelView->_12 + y * modelView->_22 + z * modelView->_32 + modelView->_42;
			const float tz = x * modelView->_13 + y * modelView->_23 + z * modelView->_33 + modelView->_43;
			const float dist = tx * plane[0] + ty * plane[1] + tz * plane[2] + plane[3];
			if (dist >= 0.0f)
			{
				return false;
			}
		}

		static int s_clipDrawLogBudget = 16;
		if (s_clipDrawLogBudget > 0)
		{
			XBLF("JA: fakegl CPU clip skipped draw mode=0x%08x verts=%lu plane=%g,%g,%g,%g",
				(unsigned int)m_drawMode,
				(unsigned long)m_vertexCount,
				plane[0], plane[1], plane[2], plane[3]);
			--s_clipDrawLogBudget;
		}
		return true;
	}
#endif

	inline IsMergableMode(GLenum mode)
	{
		return ( mode == m_drawMode ) && ( mode == GL_QUADS || mode == GL_TRIANGLES );
	}

	inline void Begin(GLuint drawMode)
	{
		m_drawMode = drawMode;
	}

	inline void Append(GLuint drawMode)
	{
	}

	inline void End(
#ifdef _XBOX
		bool clipPlane0Enabled = false,
		const float *clipPlane0 = NULL,
		const D3DMATRIX *modelView = NULL
#endif
		)
	{
		if ( m_vertexCount == 0 ) // Startup
			return;

		D3DPRIMITIVETYPE dptPrimitiveType;
		switch ( m_drawMode ) 
		{
			case GL_POINTS: dptPrimitiveType = D3DPT_POINTLIST; break;
			case GL_LINES: dptPrimitiveType = D3DPT_LINELIST; break;
			case GL_LINE_STRIP: dptPrimitiveType = D3DPT_LINESTRIP; break;
			case GL_LINE_LOOP:
				dptPrimitiveType = D3DPT_LINESTRIP;
				LocalDebugBreak();  // Need to add one more point
				break;
			case GL_TRIANGLES: dptPrimitiveType = D3DPT_TRIANGLELIST; break;
			case GL_TRIANGLE_STRIP: dptPrimitiveType = D3DPT_TRIANGLESTRIP; break;
			case GL_TRIANGLE_FAN: dptPrimitiveType = D3DPT_TRIANGLEFAN; break;
			case GL_QUADS:
				if ( m_vertexCount <= 4 ) 
					dptPrimitiveType = D3DPT_TRIANGLEFAN;
				else 
				{
					dptPrimitiveType = D3DPT_TRIANGLELIST;
					ConvertQuadsToTriangles();
				}
				break;
			case GL_QUAD_STRIP:
				if ( m_vertexCount <= 4 ) 
					dptPrimitiveType = D3DPT_TRIANGLEFAN;
				else 
				{
					dptPrimitiveType = D3DPT_TRIANGLESTRIP;
					ConvertQuadStripToTriangleStrip();
				}
				break;

			case GL_POLYGON:
				dptPrimitiveType = D3DPT_TRIANGLEFAN;
				if ( m_vertexCount < 3) 
					goto exit;
				// How is this different from GL_TRIANGLE_FAN, other than
				// that polygons are planar?
				break;
			default:
				LocalDebugBreak();
				goto exit;
		}
#ifdef _XBOX
		if (clipPlane0Enabled)
		{
			if (dptPrimitiveType == D3DPT_TRIANGLELIST)
			{
				ClipTriangleListAgainstPlane(clipPlane0, modelView);
				if (m_vertexCount < 3)
				{
					goto exit;
				}
			}
			else if (IsEntireDrawOutsidePlane(clipPlane0, modelView))
			{
				goto exit;
			}
		}
#endif
		{
			DWORD primCount;
			switch ( dptPrimitiveType ) 
			{
				default:
				case D3DPT_TRIANGLESTRIP: primCount = m_vertexCount - 2; break;
				case D3DPT_TRIANGLEFAN: primCount = m_vertexCount - 2; break;
				case D3DPT_TRIANGLELIST: primCount = m_vertexCount / 3; break;
			}

#ifdef USE_PUSHBUFFER
			m_pD3DDev->BeginPushBuffer(m_pushBuffer);
#endif

#ifdef _XBOX
			static int s_xboxDrawLogCount = 0;
			const bool logDraw = false;
			{
				static int s_xboxTwoStageVertexLogCount = 0;
				const int textureStages = (m_vertexTypeDesc & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
				if (textureStages >= 2 && m_vertexCount > 0 && s_xboxTwoStageVertexLogCount < 8)
				{
					const DWORD *v = (const DWORD *)m_OGLPrimitiveVertexBuffer;
					const float x = *(const float *)&v[0];
					const float y = *(const float *)&v[1];
					const float z = *(const float *)&v[2];
					const unsigned long color = (unsigned long)v[3];
					const float s0 = *(const float *)&v[4];
					const float t0 = *(const float *)&v[5];
					const float s1 = *(const float *)&v[6];
					const float t1 = *(const float *)&v[7];
					XBLF("JA: fakegl two-stage draw sample #%d mode=0x%08x prim=%d prims=%lu verts=%lu fvf=0x%08lx stride=%d xyz=%g,%g,%g color=0x%08lx st0=%g,%g st1=%g,%g",
						s_xboxTwoStageVertexLogCount,
						(unsigned int)m_drawMode,
						(int)dptPrimitiveType,
						(unsigned long)primCount,
						(unsigned long)m_vertexCount,
						(unsigned long)m_vertexTypeDesc,
						m_vertexSize,
						x, y, z,
						color,
						s0, t0,
						s1, t1);
					++s_xboxTwoStageVertexLogCount;
				}
			}
			if (logDraw)
			{
				XBLF("JA: fakegl DrawPrimitiveUP #%d dev=%p mode=0x%08x d3dPrim=%d primCount=%lu vertexCount=%lu vtxSize=%d fvf=0x%08lx vb=%p",
					s_xboxDrawLogCount, (void*)m_pD3DDev, (unsigned int)m_drawMode, (int)dptPrimitiveType,
					(unsigned long)primCount, (unsigned long)m_vertexCount, m_vertexSize,
					(unsigned long)m_vertexTypeDesc, m_OGLPrimitiveVertexBuffer);
			}
#endif
			if (m_pD3DDev)
			{
#ifdef _XBOX
				if (logDraw)
				{
					XBLF("JA: fakegl SetVertexShader #%d fvf=0x%08lx",
						s_xboxDrawLogCount, (unsigned long)m_vertexTypeDesc);
				}
#endif
				HRESULT hrSetVertexShader = m_pD3DDev->SetVertexShader(m_vertexTypeDesc);
#ifdef _XBOX
				if (logDraw)
				{
					XBLF("JA: fakegl SetVertexShader #%d hr=0x%08lx",
						s_xboxDrawLogCount, (unsigned long)hrSetVertexShader);
				}
#endif
				HRESULT hrDrawPrimitive = DrawPrimitiveUPXbox(dptPrimitiveType, primCount, m_OGLPrimitiveVertexBuffer);
#ifdef _XBOX
				if (logDraw)
				{
					XBLF("JA: fakegl DrawPrimitiveUP #%d returned hr=0x%08lx", s_xboxDrawLogCount, (unsigned long)hrDrawPrimitive);
				}
#endif
			}
#ifdef _XBOX
			else
			{
				XBLog_Write("JA: fakegl DrawPrimitiveUP skipped because m_pD3DDev is NULL");
			}
			s_xboxDrawLogCount++;
#endif

#ifdef USE_PUSHBUFFER
			m_pD3DDev->EndPushBuffer();
			m_pD3DDev->RunPushBuffer(m_pushBuffer, NULL);
#endif
		}
exit:
		m_vertexCount = 0;
		m_count = 0;
	}

private:
	HRESULT DrawPrimitiveUPXbox(D3DPRIMITIVETYPE dptPrimitiveType, DWORD primCount, const void *vertices)
	{
#ifdef _XBOX
		static int s_xboxChunkLogCount = 0;
		static int s_xboxSubmitLogCount = 0;
		static DWORD s_xboxDrawSubmitCount = 0;
		const DWORD maxTriangleListPrims = 256;
		HRESULT firstFailure = S_OK;

		if (!m_pD3DDev)
		{
			return D3DERR_INVALIDCALL;
		}

		if (dptPrimitiveType == D3DPT_TRIANGLELIST && primCount > maxTriangleListPrims)
		{
				DWORD primBase;
				const char *base = (const char *)vertices;

				for (primBase = 0; primBase < primCount; )
				{
					DWORD chunkPrims = primCount - primBase;
					HRESULT hr;
					const bool xboxTraceSubmit = ((s_xboxDrawSubmitCount & 255) == 0);

				if (chunkPrims > maxTriangleListPrims)
				{
					chunkPrims = maxTriangleListPrims;
				}

				if (false && s_xboxChunkLogCount < 32)
				{
					XBLF("JA: fakegl DrawPrimitiveUP chunk type=%d primBase=%lu chunk=%lu total=%lu submit=%lu",
						(int)dptPrimitiveType,
						(unsigned long)primBase,
						(unsigned long)chunkPrims,
						(unsigned long)primCount,
						(unsigned long)s_xboxDrawSubmitCount);
					s_xboxChunkLogCount++;
				}

				if (xboxTraceSubmit || s_xboxSubmitLogCount < 128)
				{
					XBLF("JA: fakegl DrawPrimitiveUP submit #%lu chunk pre type=%d prims=%lu verts=%p stride=%d",
						(unsigned long)s_xboxDrawSubmitCount, (int)dptPrimitiveType,
						(unsigned long)chunkPrims, base + (primBase * 3 * m_vertexSize),
						m_vertexSize);
				}
				hr = m_pD3DDev->DrawPrimitiveUP(dptPrimitiveType,
					chunkPrims,
					base + (primBase * 3 * m_vertexSize),
					m_vertexSize);
				if (xboxTraceSubmit || s_xboxSubmitLogCount < 128)
				{
					XBLF("JA: fakegl DrawPrimitiveUP submit #%lu chunk post hr=0x%08lx",
						(unsigned long)s_xboxDrawSubmitCount, (unsigned long)hr);
				}
				if (FAILED(hr) && SUCCEEDED(firstFailure))
				{
					firstFailure = hr;
				}

				if (xboxTraceSubmit || s_xboxSubmitLogCount < 128)
				{
					XBLF("JA: fakegl DrawPrimitiveUP submit #%lu KickPushBuffer pre",
						(unsigned long)s_xboxDrawSubmitCount);
				}
				m_pD3DDev->KickPushBuffer();
				/* Avoid long-run CXBX-R stalls from per-draw GPU idle waits.
				 * SwapBuffers still performs the frame boundary idle before Present. */
				if (xboxTraceSubmit || s_xboxSubmitLogCount < 128)
				{
					XBLF("JA: fakegl DrawPrimitiveUP submit #%lu KickPushBuffer post",
						(unsigned long)s_xboxDrawSubmitCount);
					if (s_xboxSubmitLogCount < 128)
					{
						++s_xboxSubmitLogCount;
					}
				}
				Sleep(0);
				s_xboxDrawSubmitCount++;
				primBase += chunkPrims;
			}

			return firstFailure;
		}

		{
			const bool xboxTraceSubmit = ((s_xboxDrawSubmitCount & 255) == 0);
			if (xboxTraceSubmit || s_xboxSubmitLogCount < 128)
			{
				XBLF("JA: fakegl DrawPrimitiveUP submit #%lu direct pre type=%d prims=%lu verts=%p stride=%d",
					(unsigned long)s_xboxDrawSubmitCount, (int)dptPrimitiveType,
					(unsigned long)primCount, vertices, m_vertexSize);
			}
			HRESULT hr = m_pD3DDev->DrawPrimitiveUP(dptPrimitiveType, primCount, vertices, m_vertexSize);
			if (xboxTraceSubmit || s_xboxSubmitLogCount < 128)
			{
				XBLF("JA: fakegl DrawPrimitiveUP submit #%lu direct post hr=0x%08lx",
					(unsigned long)s_xboxDrawSubmitCount, (unsigned long)hr);
			}
			if (xboxTraceSubmit || s_xboxSubmitLogCount < 128)
			{
				XBLF("JA: fakegl DrawPrimitiveUP submit #%lu KickPushBuffer pre",
					(unsigned long)s_xboxDrawSubmitCount);
			}
			m_pD3DDev->KickPushBuffer();
			/* Avoid long-run CXBX-R stalls from per-draw GPU idle waits.
			 * SwapBuffers still performs the frame boundary idle before Present. */
			if (xboxTraceSubmit || s_xboxSubmitLogCount < 128)
			{
				XBLF("JA: fakegl DrawPrimitiveUP submit #%lu KickPushBuffer post",
					(unsigned long)s_xboxDrawSubmitCount);
				if (s_xboxSubmitLogCount < 128)
				{
					++s_xboxSubmitLogCount;
				}
			}
			if ((s_xboxDrawSubmitCount & 15) == 0)
			{
				Sleep(0);
			}
			s_xboxDrawSubmitCount++;
			return hr;
		}
#else
		return m_pD3DDev->DrawPrimitiveUP(dptPrimitiveType, primCount, vertices, m_vertexSize);
#endif
	}

	void ConvertQuadsToTriangles()
	{
		int quadCount = m_vertexCount / 4;
		int addedVerticies = 2 * quadCount;
		int addedDataSize = addedVerticies * m_vertexSize;
		Ensure( addedDataSize );

		// A quad is v0, v1, v2, v3
		// The corresponding triangle pair is v0 v1 v2 , v0 v2 v3
		for(int i = quadCount-1; i >= 0; i--)
		{
			int startOfQuad = i * m_vertexSize * 4;
			int startOfTrianglePair = i * m_vertexSize * 6;
			// Copy the last two verticies of the second triangle
			memcpy(m_OGLPrimitiveVertexBuffer + startOfTrianglePair + 4 * m_vertexSize,
				m_OGLPrimitiveVertexBuffer + startOfQuad + m_vertexSize * 2, m_vertexSize * 2);
			// Copy the first vertex of the second triangle
			memcpy(m_OGLPrimitiveVertexBuffer + startOfTrianglePair + 3 * m_vertexSize,
				m_OGLPrimitiveVertexBuffer + startOfQuad, m_vertexSize);
			// Copy the first triangle
			if ( i > 0 ) 
			{
				memcpy(m_OGLPrimitiveVertexBuffer + startOfTrianglePair, m_OGLPrimitiveVertexBuffer + startOfQuad, 3 * m_vertexSize);
			}
		}
		m_count += addedDataSize;
		m_vertexCount += addedVerticies;
	}

	void ConvertQuadStripToTriangleStrip()
	{
		int vertexPairCount = m_vertexCount / 2;

		// Doesn't add any points, but does reorder the vertices.
		// Swap each pair of verticies.

		for(int i = 0; i < vertexPairCount; i++) 
		{
			int startOfPair = i * m_vertexSize * 2;
			int middleOfPair = startOfPair + m_vertexSize;
			for(int j = 0; j < m_vertexSize; j++) 
			{
				int c = m_OGLPrimitiveVertexBuffer[startOfPair + j];
				m_OGLPrimitiveVertexBuffer[startOfPair + j] = m_OGLPrimitiveVertexBuffer[middleOfPair + j];
				m_OGLPrimitiveVertexBuffer[middleOfPair + j] = c;
			}
		}
	}

	void Ensure(int size)
	{
		if (( m_count + size ) > m_size ) 
		{
			int newSize = m_size * 2;
			if ( newSize < m_count + size ) newSize = m_count + size;
			char* newVB = new char[newSize];
			if ( m_OGLPrimitiveVertexBuffer )
			{
				memcpy(newVB, m_OGLPrimitiveVertexBuffer, m_count);
			}
			delete[] m_OGLPrimitiveVertexBuffer;
			m_OGLPrimitiveVertexBuffer = newVB;
			m_size = newSize;
		}

		/*
		char buf[512];
		sprintf(buf, "Current size %d\n", m_size);
		OutputDebugString(buf);
		*/
	}

	GLuint m_drawMode;
	DWORD  m_vertexTypeDesc;
	int m_vertexSize; // in bytes

	IDirect3DDevice8* m_pD3DDev;
	char* m_OGLPrimitiveVertexBuffer;
	int m_size;  // bytes size of buffer
	int m_count; // bytes used
	DWORD m_vertexCount;
	D3DCOLOR m_color;
	float m_textureCoords[MAXSTATES*2];
	IDirect3DPushBuffer8* m_pushBuffer;
};

#endif // USE_DRAWPRIMITIVE

class FakeGL
{
private:
	IDirect3DDevice8* m_pD3DDev;
    D3DSURFACE_DESC m_d3dsdBackBuffer;   // Surface desc of the backbuffer
	LPDIRECT3D8 m_pD3D;
	
	IDirect3DTexture8* m_pPrimary;
	IDirect3DTexture8* m_fallbackTexture;
	bool m_hardwareTandL;
	
    bool m_bD3DXReady;
	
	bool m_glRenderStateDirty;

	bool m_glAlphaStateDirty;
	GLenum m_glAlphaFunc;
	GLclampf m_glAlphaFuncRef;
	bool m_glAlphaTest;

	bool m_glBlendStateDirty;
	bool m_glBlend;
	GLenum m_glBlendFuncSFactor;
	GLenum m_glBlendFuncDFactor;

	bool m_glCullStateDirty;
	bool m_glCullFace;
	GLenum m_glCullFaceMode;
	
	bool m_glDepthStateDirty;
	bool m_glDepthTest;
	GLenum m_glDepthFunc;
	bool m_glDepthMask;
	bool m_glClipPlane0StateDirty;
	bool m_glClipPlane0Enabled;
	float m_glClipPlane0[4];
	bool m_glScissorTest;
	D3DRECT m_glScissorRect;
	bool m_glFogStateDirty;
	bool m_glFog;
	GLint m_glFogMode;
	GLfloat m_glFogDensity;
	GLfloat m_glFogStart;
	GLfloat m_glFogEnd;
	D3DCOLOR m_glFogColor;

	GLclampd m_glDepthRangeNear;
	GLclampd m_glDepthRangeFar;

	GLenum m_glMatrixMode;

	GLenum m_glPolygonModeFront;
	GLenum m_glPolygonModeBack;

	bool m_glShadeModelStateDirty;
	GLenum m_glShadeModel;

	bool m_bViewPortDirty;
	GLint m_glViewPortX;
	GLint m_glViewPortY;
	GLsizei m_glViewPortWidth;
	GLsizei m_glViewPortHeight;
	D3DCOLOR m_glClearColor;

	TextureState m_textureState;
	TextureTable m_textures;

	bool m_modelViewMatrixStateDirty;
	bool m_projectionMatrixStateDirty;
	bool m_textureMatrixStateDirty;
	bool* m_currentMatrixStateDirty; // an alias to one of the preceeding stacks

	ID3DXMatrixStack* m_modelViewMatrixStack;
	ID3DXMatrixStack* m_projectionMatrixStack;
	ID3DXMatrixStack* m_textureMatrixStack;
	ID3DXMatrixStack* m_currentMatrixStack; // an alias to one of the preceeding stacks

	bool m_viewMatrixStateDirty;
	D3DXMATRIX m_d3dViewMatrix;

	OGLPrimitiveVertexBuffer m_OGLPrimitiveVertexBuffer;

	bool m_needBeginScene;

	const char* m_vendor;
	const char* m_renderer;
	char m_version[64];
	const char* m_extensions;
	D3DADAPTER_IDENTIFIER8 m_dddi;
	DWORD m_windowHeight;

	char* m_stickyAlloc;
	DWORD m_stickyAllocSize;

	bool m_hintGenerateMipMaps;

	HRESULT ReleaseD3DX()
	{
		m_bD3DXReady = FALSE;
		return S_OK;
	}
	
	HRESULT InitD3DX()
	{
		HRESULT hr;

#ifdef _XBOX
		XBL("JA: fakegl InitD3DX enter\n");
#endif
		m_pD3D = Direct3DCreate8( D3D_SDK_VERSION );
#ifdef _XBOX
		XBLF("JA: fakegl Direct3DCreate8 -> %p\n", (void*)m_pD3D);
#endif

		// Set up the structure used to create the D3DDevice.
		D3DPRESENT_PARAMETERS params; 
		ZeroMemory( &params, sizeof(params) );

		// Set up the parameters
		params.EnableAutoDepthStencil = TRUE;
		/* Raven/VV's Xbox DX8 present path and the retail SP XBE both use
		 * the linear D24S8 depth/stencil format.  Matching it matters for
		 * stable depth ordering on Xbox/Cxbx rather than merely allocating
		 * a depth surface with the same bit count. */
		params.AutoDepthStencilFormat = D3DFMT_LIN_D24S8;
		/* CXBX-R cross-project audit: SwapEffect_DISCARD (not FLIP) +
		 * PresentationInterval_IMMEDIATE (not default ONE) + BackBufferCount=1
		 * + Windowed=FALSE + hDeviceWindow=NULL.  This is the identical
		 * D3DPRESENT_PARAMETERS block UT99-Xbox / TheForceEngine / OpenJKDF2
		 * all use.  fakegl's FLIP+ONE default works on real silicon (eventually)
		 * but CXBX-R's HLE vsync emulation deadlocks. */
		params.SwapEffect             = D3DSWAPEFFECT_DISCARD;
		params.BackBufferCount        = 1;
		params.BackBufferWidth        = gWidth;
		params.BackBufferHeight       = gHeight;
		params.BackBufferFormat       = D3DFMT_X8R8G8B8;
		params.Windowed               = FALSE;
		params.hDeviceWindow          = NULL;
		params.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
		params.FullScreen_RefreshRateInHz = 60;

		/* CXBX-R cross-project audit: SetPushBufferSize REMOVED.
		 * UT99-Xbox/XboxRender.cpp:438-443 explicitly comments that
		 * calling SetPushBufferSize collides with auto-depth-stencil
		 * allocation and is unnecessary on Xbox D3D8 (the default is
		 * already correct).  Neither TheForceEngine nor OpenJKDF2 calls
		 * it either.  Letting the runtime pick its default fixes a
		 * known CXBX-R HLE allocator interaction. */
		// Create the Direct3D device.
		/* CXBX-R cross-project audit: add D3DCREATE_PUREDEVICE.  All three
		 * reference projects use HARDWARE_VERTEXPROCESSING | PUREDEVICE.
		 * Pure-device is the canonical Xbox D3D8 path; without it, the
		 * runtime tracks redundant state shadow copies that CXBX-R HLE
		 * doesn't always emulate cleanly. */
		if ( m_pD3D )
		{
			hr =  m_pD3D->CreateDevice( 0, D3DDEVTYPE_HAL, NULL,
			                            D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_PUREDEVICE,
			                            &params, &m_pD3DDev );
		}
		else
		{
			hr = Direct3D_CreateDevice( 0, D3DDEVTYPE_HAL, NULL,
			                            D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_PUREDEVICE,
			                            &params, &m_pD3DDev );
		}
#ifdef _XBOX
		XBLF("JA: fakegl CreateDevice -> hr=0x%08x dev=%p via=%s\n",
			(unsigned int)hr, (void*)m_pD3DDev, m_pD3D ? "d3d" : "static");
#endif
		if( FAILED(hr) )
			return hr;

		// Store render target surface desc.  Cxbx-R's HLE path can create the
		// device successfully and still fault when querying the implicit
		// backbuffer surface here; the values are the present params we own.
		ZeroMemory(&m_d3dsdBackBuffer, sizeof(m_d3dsdBackBuffer));
		m_d3dsdBackBuffer.Format = params.BackBufferFormat;
		m_d3dsdBackBuffer.Width = params.BackBufferWidth;
		m_d3dsdBackBuffer.Height = params.BackBufferHeight;
		m_d3dsdBackBuffer.Type = D3DRTYPE_SURFACE;
#ifdef _XBOX
		XBLF("JA: fakegl backbuffer fmt=0x%08x size=%ux%u\n",
			(unsigned int)m_d3dsdBackBuffer.Format,
			(unsigned int)m_d3dsdBackBuffer.Width,
			(unsigned int)m_d3dsdBackBuffer.Height);
#endif

		// Apply visual improvements
		m_pD3DDev->SetFlickerFilter(1);
		m_pD3DDev->SetSoftDisplayFilter(false);

		// We are done here
		m_bD3DXReady = TRUE;
#ifdef _XBOX
		EnsureFallbackTexture();
#endif
#ifdef _XBOX
		XBL("JA: fakegl InitD3DX done\n");
#endif

		return hr;
	}
	
	void InterpretError(HRESULT hr)
	{
		char errStr[100];
		D3DXGetErrorString(hr, errStr, sizeof(errStr) / sizeof(errStr[0]) );
#ifdef _XBOX
		XBLF("JA: fakegl HRESULT 0x%08x %s\n", (unsigned int)hr, errStr);
#endif
		OutputDebugString(errStr);
		LocalDebugBreak();
	}

	int BytesPerPixel(D3DFORMAT format)
	{
		switch ( format ) 
		{
			case D3DFMT_P8:
			case D3DFMT_AL8:
			case D3DFMT_A8:
			case D3DFMT_L8:
				return 1;

			case D3DFMT_R5G6B5:
			case D3DFMT_A4R4G4B4:
			case D3DFMT_A8L8:
				return 2;

			case D3DFMT_A8R8G8B8:
			case D3DFMT_X8R8G8B8:
				return 4;

			case D3DFMT_DXT1:
			case D3DFMT_DXT3:
			case D3DFMT_DXT5:
				return 0;

			default:
				LocalDebugBreak();
				return 4;
		}
	}

	DWORD DDSLevelRowBytes(D3DFORMAT format, DWORD width)
	{
		switch ( format )
		{
			case D3DFMT_DXT1:
				return ((width + 3) >> 2) * 8;
			case D3DFMT_DXT3:
			case D3DFMT_DXT5:
				return ((width + 3) >> 2) * 16;
			default:
				return width * BytesPerPixel(format);
		}
	}

	DWORD DDSLevelRows(D3DFORMAT format, DWORD height)
	{
		switch ( format )
		{
			case D3DFMT_DXT1:
			case D3DFMT_DXT3:
			case D3DFMT_DXT5:
				return (height + 3) >> 2;
			default:
				return height;
		}
	}

#ifdef _XBOX
	DWORD EstimateTextureBytes(D3DFORMAT format, DWORD width, DWORD height, int levels)
	{
		DWORD total = 0;
		DWORD levelWidth = width;
		DWORD levelHeight = height;
		if (levels <= 0)
			levels = 1;
		for (int level = 0; level < levels; ++level)
		{
			total += DDSLevelRowBytes(format, levelWidth) * DDSLevelRows(format, levelHeight);
			if (levelWidth > 1)
				levelWidth >>= 1;
			if (levelHeight > 1)
				levelHeight >>= 1;
		}
		return total;
	}

	int DDSAvailableLevels(D3DFORMAT format, DWORD width, DWORD height, DWORD bytes)
	{
		int levels = 0;
		while (width > 0 && height > 0)
		{
			DWORD levelBytes = DDSLevelRowBytes(format, width) * DDSLevelRows(format, height);
			if (!levelBytes || bytes < levelBytes)
				break;
			bytes -= levelBytes;
			++levels;
			if (width == 1 && height == 1)
				break;
			if (width > 1)
				width >>= 1;
			if (height > 1)
				height >>= 1;
		}
		return levels;
	}

	void TrackTextureAlloc(const char *kind, DWORD bytes)
	{
		g_fakeglTextureCount++;
		g_fakeglTextureBytes += bytes;
		if (g_fakeglTextureCount <= 16 || (g_fakeglTextureCount & 63) == 0)
		{
			XBLF("JA: fakegl texture budget kind=%s count=%u bytes=%u totalKB=%u failures=%u\n",
				kind, g_fakeglTextureCount, bytes, (DWORD)(g_fakeglTextureBytes / 1024),
				g_fakeglTextureFailures);
		}
	}

	void TrackTextureFailure(void)
	{
		g_fakeglTextureFailures++;
		XBLF("JA: fakegl texture allocation failures=%u\n", g_fakeglTextureFailures);
	}
#endif

	HRESULT CreateXboxTexture(DWORD width, DWORD height, DWORD levels, DWORD usage,
		D3DFORMAT format, IDirect3DTexture8** texture)
	{
		if (!texture)
			return E_FAIL;
		*texture = NULL;
#ifdef _XBOX
		D3DTexture* created = D3DDevice_CreateTexture2(width, height, 1, levels, usage, format, D3DRTYPE_TEXTURE);
		if (!created)
			return E_OUTOFMEMORY;
		*texture = created;
		return S_OK;
#else
		return m_pD3DDev->CreateTexture(width, height, levels, usage, format, D3DPOOL_MANAGED, texture);
#endif
	}

#ifdef _XBOX
	HRESULT CreateRegisteredXboxTexture(DWORD width, DWORD height, DWORD levels, DWORD usage,
		D3DFORMAT format, IDirect3DTexture8** texture, DWORD* textureBytes, void** textureData = NULL)
	{
		if (!texture)
			return E_FAIL;
		*texture = NULL;
		if (textureBytes)
			*textureBytes = 0;
		if (textureData)
			*textureData = NULL;

		D3DTexture* header = new D3DTexture;
		if (!header)
			return E_OUTOFMEMORY;
		ZeroMemory(header, sizeof(*header));

		DWORD bytes = XGSetTextureHeader(width, height, levels, usage, format,
			D3DPOOL_DEFAULT, header, 0, 0);
		if (!bytes)
		{
			delete header;
			return E_FAIL;
		}

		void* textureMemory = NULL;
		try
		{
			textureMemory = JkaStaticTextureAlloc(bytes, (GLuint)m_textures.GetCurrentID());
		}
		catch (...)
		{
			textureMemory = NULL;
		}
		if (!textureMemory)
		{
			XBLF("JA: fakegl registered DDS pool allocation failed tex=%d bytes=%u\n",
				m_textures.GetCurrentID(), bytes);
			delete header;
			return E_OUTOFMEMORY;
		}

		header->Register(textureMemory);
		*texture = header;
		if (textureBytes)
			*textureBytes = bytes;
		if (textureData)
			*textureData = textureMemory;
		return S_OK;
	}
#endif

	void UploadFallbackTexture(IDirect3DTexture8* texture)
	{
		WORD pixels[8 * 8];
		for ( int y = 0; y < 8; ++y )
		{
			for ( int x = 0; x < 8; ++x )
			{
				pixels[y * 8 + x] = ((x ^ y) & 1) ? 0xffff : 0xf0ff;
			}
		}

		D3DLOCKED_RECT rect;
		HRESULT hr = texture->LockRect(0, &rect, NULL, 0);
		if ( FAILED(hr) )
		{
			InterpretError(hr);
			return;
		}

		XGSwizzleRect(pixels, 8 * sizeof(WORD), NULL, rect.pBits, 8, 8, NULL, sizeof(WORD));
		texture->UnlockRect(0);
	}

	bool EnsureFallbackTexture(void)
	{
		if (!m_pD3DDev)
		{
			return false;
		}

		if (!m_fallbackTexture)
		{
			HRESULT fallbackHr = CreateXboxTexture(8, 8, 1, 0, D3DFMT_A4R4G4B4, &m_fallbackTexture);
			if (FAILED(fallbackHr) || !m_fallbackTexture)
			{
#ifdef _XBOX
				XBLF("JA: fakegl fallback texture prewarm failed hr=0x%08x\n", (unsigned int)fallbackHr);
#endif
				return false;
			}
			UploadFallbackTexture(m_fallbackTexture);
#ifdef _XBOX
			TrackTextureAlloc("fallback-shared", EstimateTextureBytes(D3DFMT_A4R4G4B4, 8, 8, 1));
			XBL("JA: fakegl fallback texture ready\n");
#endif
		}
		return true;
	}

	bool UseFallbackTexture(D3DFORMAT d3dFormat, GLint internalFormat)
	{
		if (!EnsureFallbackTexture())
		{
			return false;
		}

		m_fallbackTexture->AddRef();
		m_textures.SetTexture(m_fallbackTexture, D3DFMT_A4R4G4B4, GL_RGBA4);
		m_textureState.DirtyTexture(m_textures.GetCurrentID());
		return true;
	}

public:
	void DeleteTexture(GLuint id)
	{
		m_textures.DeleteTexture(id);
		m_textureState.DirtyTexture(id);
#ifdef _XBOX
		static int s_deleteLogs = 0;
		if (s_deleteLogs < 64)
		{
			XBLF("JA: fakegl DeleteTexture tex=%u\n", id);
			++s_deleteLogs;
		}
#endif
	}

	bool IsTexture(GLuint id)
	{
		return m_textures.IsTexture(id);
	}

	FakeGL(/*HWND hwndMain*/)
	{
		//m_hwndMain = hwndMain;

		m_windowHeight = 480; //FIXME
		m_bD3DXReady = TRUE;

		m_pD3DDev = 0;
		m_pD3D = 0;
		m_pPrimary = 0;
		m_fallbackTexture = 0;
		m_hardwareTandL = false;
		m_modelViewMatrixStack = 0;
		m_projectionMatrixStack = 0;
		m_textureMatrixStack = 0;
		m_currentMatrixStack = 0;
		m_stickyAlloc = 0;
		m_stickyAllocSize = 0;

		m_glRenderStateDirty = true;

		m_glAlphaStateDirty = true;
		m_glAlphaFunc = GL_ALWAYS;
		m_glAlphaFuncRef = 0;
		m_glAlphaTest = false;

		m_glBlendStateDirty = true;
		m_glBlend = false;
		m_glBlendFuncSFactor = GL_ONE; // Not sure this is the default
		m_glBlendFuncDFactor = GL_ZERO; // Not sure this is the default
	
		m_glCullStateDirty = true;
		m_glCullFace = false;

		m_glCullFaceMode = GL_BACK;
		
		m_glDepthStateDirty = true;
		m_glDepthTest = false;
		m_glDepthMask = true;
		m_glDepthFunc = GL_ALWAYS; // not sure if this is the default
		m_glClipPlane0StateDirty = true;
		m_glClipPlane0Enabled = false;
		m_glClipPlane0[0] = 0.0f;
		m_glClipPlane0[1] = 0.0f;
		m_glClipPlane0[2] = 0.0f;
		m_glClipPlane0[3] = 0.0f;
		m_glScissorTest = false;
		m_glScissorRect.x1 = 0;
		m_glScissorRect.y1 = 0;
		m_glScissorRect.x2 = (LONG)gWidth;
		m_glScissorRect.y2 = (LONG)gHeight;
		m_glFogStateDirty = true;
		m_glFog = false;
		m_glFogMode = GL_EXP;
		m_glFogDensity = 1.0f;
		m_glFogStart = 0.0f;
		m_glFogEnd = 1.0f;
		m_glFogColor = D3DCOLOR_ARGB(0, 0, 0, 0);

		m_glDepthRangeNear = 0; // not sure if this is the default
		m_glDepthRangeFar = 1.0; // not sure if this is the default

		m_glMatrixMode = GL_MODELVIEW; // Not sure this is the default

		m_glPolygonModeFront = GL_FILL;
		m_glPolygonModeBack = GL_FILL;

		m_glShadeModelStateDirty = true;
		m_glShadeModel = GL_SMOOTH;

		m_bViewPortDirty = true;
		m_glViewPortX = 0;
		m_glViewPortY = 0;
							
		m_glViewPortWidth = gWidth;//640;//Marty FIXME
		m_glViewPortHeight = gHeight;//480;
		m_glClearColor = D3DRGBA(0, 0, 0, 0);

		m_vendor = 0;
		m_renderer = 0;
		m_extensions = 0;

		m_hintGenerateMipMaps = true;
		
		InitD3DX();
		
		D3DXCreateMatrixStack(0, &m_modelViewMatrixStack);
		D3DXCreateMatrixStack(0, &m_projectionMatrixStack);
		D3DXCreateMatrixStack(0, &m_textureMatrixStack);
		m_currentMatrixStack = m_modelViewMatrixStack;
		m_modelViewMatrixStack->LoadIdentity(); // Not sure this is correct
		m_projectionMatrixStack->LoadIdentity();
		m_textureMatrixStack->LoadIdentity();
		m_modelViewMatrixStateDirty = true;
		m_projectionMatrixStateDirty = true;
		m_textureMatrixStateDirty = true;
		m_currentMatrixStateDirty = &m_modelViewMatrixStateDirty;
		m_viewMatrixStateDirty = true;

		D3DXMatrixIdentity(&m_d3dViewMatrix);

		m_needBeginScene = true;

		{
			// Check for multitexture.
			D3DCAPS8 deviceCaps;
			HRESULT hr = m_pD3DDev->GetDeviceCaps(&deviceCaps);
			if ( ! FAILED(hr)) 
			{
				// Clamp texture blend stages to 2. Some cards can do eight, but that's more
				// than we need.
				int maxStages = deviceCaps.MaxTextureBlendStages;
				if ( maxStages > 2 )
				{
					maxStages = 2;
				}
				m_textureState.SetMaxStages(maxStages);

				m_hardwareTandL = (deviceCaps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) != 0;
			}
		}

		// One-time render state initialization
		m_pD3DDev->SetRenderState( D3DRS_TEXTUREFACTOR, 0x00000000 );
		m_pD3DDev->SetRenderState( D3DRS_DITHERENABLE, 0 ); //FALSE looks worse in 16 bit mode (D3DFMT_X1R5G5B5)
		m_pD3DDev->SetRenderState( D3DRS_SPECULARENABLE, FALSE );
		m_pD3DDev->SetRenderState( D3DRS_LIGHTING, FALSE);
		m_pD3DDev->SetRenderState( D3DRS_FOGENABLE, FALSE);
	}
	~FakeGL()
	{
		delete [] m_stickyAlloc;
		RELEASENULL(m_fallbackTexture);
		ReleaseD3DX();
		RELEASENULL(m_modelViewMatrixStack);
		RELEASENULL(m_projectionMatrixStack);
		RELEASENULL(m_textureMatrixStack);
	}

	void glAlphaFunc (GLenum func, GLclampf ref)
	{
		if ( m_glAlphaFunc != func || m_glAlphaFuncRef != ref )
		{
			SetRenderStateDirty();
			m_glAlphaFunc = func;
			m_glAlphaFuncRef = ref;
			m_glAlphaStateDirty = true;
		}
	}
	
	void glBegin (GLenum mode)
	{
		if ( m_needBeginScene )
		{
			m_needBeginScene = false;
			HRESULT hr = m_pD3DDev->BeginScene();
			if ( FAILED(hr) )
			{
				InterpretError(hr);
			}
		}

#if 0
		// statistics
		static int beginCount;
		static int stateChangeCount;
		static int primitivesCount;
		beginCount++;
		if ( m_glRenderStateDirty )
			stateChangeCount++;
		if ( m_glRenderStateDirty || ! m_OGLPrimitiveVertexBuffer.IsMergableMode(mode) )
			primitivesCount++;
#endif
		if ( m_glRenderStateDirty || ! m_OGLPrimitiveVertexBuffer.IsMergableMode(mode) ) 
		{
#ifdef _XBOX
			{
				static int s_xboxBeginStateLogCount = 0;
				static int s_xboxBeginStage1LogCount = 0;
				const bool beginStage1Active = m_textureState.GetStageTexture2D(1);
				if (s_xboxBeginStateLogCount < 96 || (beginStage1Active && s_xboxBeginStage1LogCount < 96))
				{
					XBLF("JA: fakegl glBegin state mode=0x%08x dirty=%d textureDirty=%d mergable=%d maxStages=%d currentStage=%d stage0 dirty=%d tex=%u enabled=%d env=0x%08x stage1 dirty=%d tex=%u enabled=%d env=0x%08x",
						(unsigned int)mode,
						m_glRenderStateDirty ? 1 : 0,
						m_textureState.GetDirty() ? 1 : 0,
						m_OGLPrimitiveVertexBuffer.IsMergableMode(mode) ? 1 : 0,
						m_textureState.GetMaxStages(),
						m_textureState.GetCurrentStage(),
						m_textureState.GetStageDirty(0) ? 1 : 0,
						(unsigned int)m_textureState.GetStageTexture(0),
						m_textureState.GetStageTexture2D(0) ? 1 : 0,
						(unsigned int)m_textureState.GetStageTextEnvMode(0),
						m_textureState.GetStageDirty(1) ? 1 : 0,
						(unsigned int)m_textureState.GetStageTexture(1),
						m_textureState.GetStageTexture2D(1) ? 1 : 0,
						(unsigned int)m_textureState.GetStageTextEnvMode(1));
					++s_xboxBeginStateLogCount;
					if (beginStage1Active)
					{
						++s_xboxBeginStage1LogCount;
					}
				}
			}
#endif
			if (m_textureState.GetStageTexture2D(1))
			{
				m_textureState.ForceStageDirty(1);
#ifdef _XBOX
				{
					static int s_xboxForceStage1LogCount = 0;
					if (s_xboxForceStage1LogCount < 8)
					{
						XBLF("JA: fakegl force stage1 dirty before apply textureDirty=%d maxStages=%d currentStage=%d stage1 dirty=%d tex=%u enabled=%d env=0x%08x",
							m_textureState.GetDirty() ? 1 : 0,
							m_textureState.GetMaxStages(),
							m_textureState.GetCurrentStage(),
							m_textureState.GetStageDirty(1) ? 1 : 0,
							(unsigned int)m_textureState.GetStageTexture(1),
							m_textureState.GetStageTexture2D(1) ? 1 : 0,
							(unsigned int)m_textureState.GetStageTextEnvMode(1));
						++s_xboxForceStage1LogCount;
					}
				}
#endif
			}
			internalEnd();
			SetGLRenderState();
			DWORD typeDesc;
			typeDesc = D3DFVF_XYZ | D3DFVF_DIFFUSE;
			typeDesc |= (m_textureState.GetMaxStages() << D3DFVF_TEXCOUNT_SHIFT);

			if ( typeDesc != m_OGLPrimitiveVertexBuffer.GetVertexTypeDesc()
				|| !m_OGLPrimitiveVertexBuffer.HasDevice()) 
			{
				m_OGLPrimitiveVertexBuffer.Initialize(m_pD3DDev, m_pD3D, m_hardwareTandL, typeDesc);
			}
			m_OGLPrimitiveVertexBuffer.Begin(mode);
		}
		else
		{
			m_OGLPrimitiveVertexBuffer.Append(mode);
		}
	}

	void glBindTexture(GLenum target, GLuint texture)
	{
		if ( target != GL_TEXTURE_2D ) 
		{
			LocalDebugBreak();
			return;
		}
		if ( m_textureState.GetCurrentTexture() != texture ) 
		{
#ifdef _XBOX
			{
				static int s_xboxBindTextureLogCount = 0;
				if (s_xboxBindTextureLogCount < 96 || (m_textureState.GetCurrentStage() == 1 && s_xboxBindTextureLogCount < 192))
				{
					XBLF("JA: fakegl glBindTexture stage=%d old=%u new=%u target=0x%08x",
						m_textureState.GetCurrentStage(),
						(unsigned int)m_textureState.GetCurrentTexture(),
						(unsigned int)texture,
						(unsigned int)target);
					++s_xboxBindTextureLogCount;
				}
			}
#endif
			SetRenderStateDirty();
			m_textureState.SetCurrentTexture(texture);
			m_textures.BindTexture(texture);
		}
	}

	inline void glMTexCoord2fSGIS(GLenum target, GLfloat s, GLfloat t)
	{
		int textStage = target - TEXTURE0_SGIS;
		m_OGLPrimitiveVertexBuffer.SetTextureCoord(textStage, s, t);
	}
	
	inline void glSelectTextureSGIS(GLenum target)
	{
		int textStage = target - TEXTURE0_SGIS;
		m_textureState.SetCurrentStage(textStage);
		m_textures.BindTexture(m_textureState.GetCurrentTexture());
#ifdef _XBOX
		{
			static int s_xboxSelectTextureLogCount = 0;
			if (s_xboxSelectTextureLogCount < 128 || (textStage == 1 && s_xboxSelectTextureLogCount < 256))
			{
				XBLF("JA: fakegl select texture target=0x%08x stage=%d currentTex=%u enabled=%d",
					(unsigned int)target,
					textStage,
					(unsigned int)m_textureState.GetCurrentTexture(),
					m_textureState.GetTexture2D() ? 1 : 0);
				++s_xboxSelectTextureLogCount;
			}
		}
#endif
		// Does not, by itself, dirty the render state
	}

	void glBlendFunc (GLenum sfactor, GLenum dfactor)
	{
		if ( m_glBlendFuncSFactor != sfactor || m_glBlendFuncDFactor != dfactor ) 
		{
			SetRenderStateDirty();
			m_glBlendFuncSFactor = sfactor;
			m_glBlendFuncDFactor = dfactor;
			m_glBlendStateDirty = true;
		}
	}

	inline void glClear (GLbitfield mask)
	{
		internalEnd();
		SetGLRenderState();
		DWORD clearMask = 0;

		if ( mask & GL_COLOR_BUFFER_BIT )
			clearMask |= D3DCLEAR_TARGET;

		if ( mask & GL_DEPTH_BUFFER_BIT ) 
			clearMask |= D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL; 

		// see Depth-Buffer Compression and Performance Implications in the docs!
		// Quake does not use a stencil buffer, but we need to clear it anyways else
		// the performance will go down

		if ( mask & GL_STENCIL_BUFFER_BIT )
			clearMask |= D3DCLEAR_STENCIL;

		m_pD3DDev->Clear(0, NULL, clearMask, m_glClearColor, 1.0f, 0 );
	}

	void glClearColor (GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
	{
		m_glClearColor = D3DRGBA(Clamp(red), Clamp(green), Clamp(blue), Clamp(alpha));
	}

	inline void glColor3f (GLfloat red, GLfloat green, GLfloat blue)
	{
		// Note: On x86 architectures this function will chew up a lot of time
		// converting floating point to integer by calling _ftol
		// unless the /QIfist flag is specified.
		m_OGLPrimitiveVertexBuffer.SetColor(D3DRGB(Clamp(red), Clamp(green), Clamp(blue)));
	}

	inline void glColor3ubv (const GLubyte *v)
	{
		m_OGLPrimitiveVertexBuffer.SetColor(RGBA_MAKE(v[0], v[1], v[2], 0xff));
	}

	inline void glColor4ubv (const GLubyte *v)
	{
		m_OGLPrimitiveVertexBuffer.SetColor(RGBA_MAKE(v[0], v[1], v[2], v[3]));
	}

	inline void glColor4f (GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
	{
		// Note: On x86 architectures this function will chew up a lot of time
		// converting floating point to integer by calling _ftol
		// unless the /QIfist flag is specified.
		m_OGLPrimitiveVertexBuffer.SetColor(D3DRGBA(Clamp(red), Clamp(green), Clamp(blue), Clamp(alpha)));
	}

	inline void glColor4fv (const GLfloat *v)
	{
		// Note: On x86 architectures this function will chew up a lot of time
		// converting floating point to integer by calling _ftol
		// unless the /QIfist flag is specified.
		m_OGLPrimitiveVertexBuffer.SetColor(D3DRGBA(Clamp(v[0]), Clamp(v[1]), Clamp(v[2]), Clamp(v[3])));
	}

	inline void glColor4ub (GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha)
	{
		m_OGLPrimitiveVertexBuffer.SetColor(RGBA_MAKE(red, green, blue, alpha));
	}

	void glClipPlane0( const GLdouble *equation )
	{
		if ( equation )
		{
			m_glClipPlane0[0] = (float)equation[0];
			m_glClipPlane0[1] = (float)equation[1];
			m_glClipPlane0[2] = (float)equation[2];
			m_glClipPlane0[3] = (float)equation[3];
			m_glClipPlane0StateDirty = true;
			SetRenderStateDirty();
#ifdef _XBOX
			static int s_clipPlaneSetLogCount = 0;
			if (s_clipPlaneSetLogCount < 16)
			{
				XBLF("JA: fakegl GL_CLIP_PLANE0 plane=%g,%g,%g,%g",
					m_glClipPlane0[0], m_glClipPlane0[1],
					m_glClipPlane0[2], m_glClipPlane0[3]);
			}
			++s_clipPlaneSetLogCount;
#endif
		}
	}

	void glScissor(GLint x, GLint y, GLsizei width, GLsizei height)
	{
		if (x < 0) x = 0;
		else if ((DWORD)x > gWidth) x = (GLint)gWidth;
		if (y < 0) y = 0;
		else if ((DWORD)y > gHeight) y = (GLint)gHeight;
		if (width < 0) width = 0;
		else if ((DWORD)(x + width) > gWidth) width = (GLsizei)(gWidth - x);
		if (height < 0) height = 0;
		else if ((DWORD)(y + height) > gHeight) height = (GLsizei)(gHeight - y);

		y = (GLint)gHeight - (y + height);
		m_glScissorRect.x1 = x;
		m_glScissorRect.y1 = y;
		m_glScissorRect.x2 = x + width;
		m_glScissorRect.y2 = y + height;

		if (m_pD3DDev && m_glScissorTest)
		{
			m_pD3DDev->SetScissors(1, FALSE, &m_glScissorRect);
		}
#ifdef _XBOX
		{
			static int s_scissorLogCount = 0;
			if (s_scissorLogCount < 16)
			{
				XBLF("JA: fakegl Scissor enabled=%d rect=%ld,%ld,%ld,%ld",
					m_glScissorTest ? 1 : 0,
					m_glScissorRect.x1, m_glScissorRect.y1,
					m_glScissorRect.x2, m_glScissorRect.y2);
			}
			++s_scissorLogCount;
		}
#endif
	}

	void glCullFace (GLenum mode)
	{
		if ( m_glCullFaceMode != mode ) 
		{
			SetRenderStateDirty();
			m_glCullFaceMode = mode;
			m_glCullStateDirty = true;
		}
	}

	void glDepthFunc (GLenum func)
	{
		if ( m_glDepthFunc != func ) 
		{
			SetRenderStateDirty();
			m_glDepthFunc = func;
			m_glDepthStateDirty = true;
		}
	}

	void glDepthMask (GLboolean flag)
	{
		if ( m_glDepthMask != (flag != 0) ) 
		{
			SetRenderStateDirty();
			m_glDepthMask = flag != 0 ? true : false;
			m_glDepthStateDirty = true;
		}
	}

	void glDepthRange (GLclampd zNear, GLclampd zFar)
	{
		if ( m_glDepthRangeNear != zNear || m_glDepthRangeFar != zFar ) 
		{
			SetRenderStateDirty();
			m_glDepthRangeNear = zNear;
			m_glDepthRangeFar = zFar;
			m_bViewPortDirty = true;
		}
	}

	void glDisable (GLenum cap)
	{
		glEnableDisableSet(cap, false);
	}

	void glDrawBuffer (GLenum /* mode */)
	{
		// Do nothing. (Can DirectX render to the front buffer at all?)
	}

	void glEnable (GLenum cap)
	{
		glEnableDisableSet(cap, true); 
	}

	void glEnableDisableSet(GLenum cap, bool value)
	{
		switch ( cap ) 
		{
		case GL_ALPHA_TEST:
			if ( m_glAlphaTest != value ) 
			{
				SetRenderStateDirty();
				m_glAlphaTest = value;
				m_glAlphaStateDirty = true;
			}
			break;
		case GL_BLEND:
			if ( m_glBlend != value )
			{
				SetRenderStateDirty();
				m_textureState.SetMainBlend(value); 
				m_glBlend = value;
				m_glBlendStateDirty = true;
			}
			break;
		case GL_CULL_FACE:
			if ( m_glCullFace != value )
			{
				SetRenderStateDirty();
				m_glCullFace = value;
				m_glCullStateDirty = true;
			}
			break;
		case GL_DEPTH_TEST:
			if ( m_glDepthTest != value ) 
			{
				SetRenderStateDirty();
				m_glDepthTest = value;
				m_glDepthStateDirty = true;
			}
		break;
		case GL_TEXTURE_2D:
			if ( m_textureState.GetTexture2D() != value )
			{
#ifdef _XBOX
				{
					static int s_xboxTexture2DLogCount = 0;
					if (s_xboxTexture2DLogCount < 64 || (m_textureState.GetCurrentStage() == 1 && s_xboxTexture2DLogCount < 160))
					{
						XBLF("JA: fakegl texture2d stage=%d value=%d old=%d tex=%u",
							m_textureState.GetCurrentStage(),
							value ? 1 : 0,
							m_textureState.GetTexture2D() ? 1 : 0,
							(unsigned int)m_textureState.GetCurrentTexture());
						++s_xboxTexture2DLogCount;
					}
				}
#endif
				SetRenderStateDirty();
				m_textureState.SetTexture2D(value);
			}
			break;
		case GL_LIGHTING:
		case GL_POLYGON_OFFSET_FILL:
			// JKA's splash/loading path saves and restores these GL caps, but
			// fakegl does not implement them. Treat them as disabled no-ops.
			break;
		case GL_SCISSOR_TEST:
			m_glScissorTest = value;
			if (m_pD3DDev)
			{
				m_pD3DDev->SetScissors(value ? 1 : 0, FALSE, &m_glScissorRect);
			}
			break;
		case GL_FOG:
			if ( m_glFog != value )
			{
				SetRenderStateDirty();
				m_glFog = value;
				m_glFogStateDirty = true;
#ifdef _XBOX
				{
					static int s_fogEnableLogCount = 0;
					if (s_fogEnableLogCount < 24)
					{
						XBLF("JA: fakegl fog %s mode=0x%04x start=%g end=%g density=%g color=0x%08x",
							value ? "enable" : "disable",
							(unsigned int)m_glFogMode,
							(float)m_glFogStart,
							(float)m_glFogEnd,
							(float)m_glFogDensity,
							(unsigned int)m_glFogColor);
					}
					++s_fogEnableLogCount;
				}
#endif
			}
			break;
		case GL_STENCIL_TEST:
			break;
		case GL_CLIP_PLANE0:
			if ( m_glClipPlane0Enabled != value )
			{
				SetRenderStateDirty();
				m_glClipPlane0Enabled = value;
				m_glClipPlane0StateDirty = true;
			}
#ifdef _XBOX
			{
				static int s_clipPlaneLogCount = 0;
				if (s_clipPlaneLogCount < 16)
				{
					XBLF("JA: fakegl GL_CLIP_PLANE0 %s no-op on Xbox fixed-function path",
						value ? "enable" : "disable");
				}
				++s_clipPlaneLogCount;
			}
#endif
			break;
		default:
#ifdef _XBOX
			{
				static int s_unknownEnableDisableCount = 0;
				if (s_unknownEnableDisableCount < 8)
				{
					XBLF("JA: fakegl unsupported gl%s cap=0x%08x",
						value ? "Enable" : "Disable", (unsigned int)cap);
				}
				s_unknownEnableDisableCount++;
			}
#endif
			LocalDebugBreak();
			break;
		}
	}

	void glEnd (void)
	{
#ifdef _XBOX
		internalEnd();
#endif
	}

	void internalEnd()
	{
		m_OGLPrimitiveVertexBuffer.EnsureDevice(m_pD3DDev);
#ifdef _XBOX
		m_OGLPrimitiveVertexBuffer.End(m_glClipPlane0Enabled, m_glClipPlane0, m_modelViewMatrixStack->GetTop());
#else
		m_OGLPrimitiveVertexBuffer.End();
#endif
	}

	void glFinish (void)
	{
		// To Do: This is supposed to flush all pending commands
		internalEnd();
	}

	void glFogf(GLenum pname, GLfloat param)
	{
		switch (pname)
		{
		case GL_FOG_DENSITY:
			m_glFogDensity = param;
			break;
		case GL_FOG_START:
			m_glFogStart = param;
			break;
		case GL_FOG_END:
			m_glFogEnd = param;
			break;
		default:
			return;
		}
		SetRenderStateDirty();
		m_glFogStateDirty = true;
#ifdef _XBOX
		{
			static int s_fogfLogCount = 0;
			if (s_fogfLogCount < 24)
			{
				XBLF("JA: fakegl fogf pname=0x%04x param=%g", (unsigned int)pname, (float)param);
			}
			++s_fogfLogCount;
		}
#endif
	}

	void glFogfv(GLenum pname, const GLfloat *params)
	{
		if (!params || pname != GL_FOG_COLOR)
		{
			return;
		}
		int r = (int)(params[0] * 255.0f);
		int g = (int)(params[1] * 255.0f);
		int b = (int)(params[2] * 255.0f);
		if (r < 0) r = 0; else if (r > 255) r = 255;
		if (g < 0) g = 0; else if (g > 255) g = 255;
		if (b < 0) b = 0; else if (b > 255) b = 255;
		m_glFogColor = D3DCOLOR_ARGB(0, r, g, b);
		SetRenderStateDirty();
		m_glFogStateDirty = true;
#ifdef _XBOX
		{
			static int s_fogfvLogCount = 0;
			if (s_fogfvLogCount < 24)
			{
				XBLF("JA: fakegl fog color %g,%g,%g -> 0x%08x",
					(float)params[0], (float)params[1], (float)params[2], (unsigned int)m_glFogColor);
			}
			++s_fogfvLogCount;
		}
#endif
	}

	void glFogi(GLenum pname, GLint param)
	{
		if (pname != GL_FOG_MODE)
		{
			return;
		}
		m_glFogMode = param;
		SetRenderStateDirty();
		m_glFogStateDirty = true;
#ifdef _XBOX
		{
			static int s_fogiLogCount = 0;
			if (s_fogiLogCount < 24)
			{
				XBLF("JA: fakegl fog mode=0x%04x", (unsigned int)param);
			}
			++s_fogiLogCount;
		}
#endif
	}
	
	void glFrustum (GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar)
	{
		SetRenderStateDirty();
		D3DXMATRIX m;
		// Note that D3D takes top, bottom arguments in opposite order
		D3DXMatrixPerspectiveOffCenterRH(&m, left, right, bottom, top, zNear, zFar);
		m_currentMatrixStack->MultMatrixLocal(&m);
		*m_currentMatrixStateDirty = true;
	}

	void glGetFloatv (GLenum pname, GLfloat *params)
	{
		switch(pname)
		{
		case GL_MODELVIEW_MATRIX:
			memcpy(params,m_modelViewMatrixStack->GetTop(), sizeof(D3DMATRIX));
			break;
		default:
			LocalDebugBreak();
			break;
		}
	}

	const GLubyte * glGetString (GLenum name)
	{
		const char* result = "";
		EnsureDriverInfo();
		switch ( name )
		{
		case GL_VENDOR:
			result = m_vendor;
			break;
		case GL_RENDERER:
			result = m_renderer;
			break;
		case GL_VERSION:
			result = m_version;
			break;
		case GL_EXTENSIONS:
			result = m_extensions;
			break;
		default:
			break;
		}
		return (const GLubyte *) result;
	}

	void glHint (GLenum /* target */, GLenum /* mode */)
	{
		LocalDebugBreak();
	}

	GLboolean glIsEnabled(GLenum cap)
	{
		switch(cap)
		{
		case GL_ALPHA_TEST:
			return  m_glAlphaTest ? 1 : 0;
		case GL_BLEND:
			return m_glBlend ? 1 : 0;
		case GL_CULL_FACE:
			return m_glCullFace ? 1 : 0;
		case GL_DEPTH_TEST:
			return m_glDepthTest ? 1 : 0;
		case GL_TEXTURE_2D:
			return m_textureState.GetTexture2D() ? 1 : 0;
		case GL_SCISSOR_TEST:
			return m_glScissorTest ? 1 : 0;
		case GL_FOG:
			return m_glFog ? 1 : 0;
		case GL_STENCIL_TEST:
		case GL_LIGHTING:
		case GL_POLYGON_OFFSET_FILL:
			return 0;
		default:
#ifdef _XBOX
			{
				static int s_unknownIsEnabledCount = 0;
				if (s_unknownIsEnabledCount < 8)
				{
					XBLF("JA: fakegl unsupported glIsEnabled cap=0x%08x", (unsigned int)cap);
				}
				s_unknownIsEnabledCount++;
			}
#endif
			return FALSE;
		}
	}

	void glLoadIdentity (void)
	{
		SetRenderStateDirty();
		m_currentMatrixStack->LoadIdentity();
		*m_currentMatrixStateDirty = true;
	}

	void glLoadMatrixf (const GLfloat *m)
	{
		SetRenderStateDirty();
		m_currentMatrixStack->LoadMatrix((D3DXMATRIX*) m);
		*m_currentMatrixStateDirty = true;
	}

	void glMatrixMode (GLenum mode)
	{
		m_glMatrixMode = mode;
		switch ( mode ) 
		{
		case GL_MODELVIEW:
			m_currentMatrixStack = m_modelViewMatrixStack;
			m_currentMatrixStateDirty = &m_modelViewMatrixStateDirty;
			break;
		case GL_PROJECTION:
			m_currentMatrixStack = m_projectionMatrixStack;
			m_currentMatrixStateDirty = &m_projectionMatrixStateDirty;
			break;
		case GL_TEXTURE:
			m_currentMatrixStack = m_textureMatrixStack;
			m_currentMatrixStateDirty = &m_textureMatrixStateDirty;
			break;
		default:
			LocalDebugBreak();
			break;
		}
	}

	void glOrtho (GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar)
	{
		SetRenderStateDirty();
		D3DXMATRIX m;
		D3DXMatrixOrthoOffCenterRH(&m, left, right, top, bottom, zNear, zFar);
		m_currentMatrixStack->MultMatrixLocal(&m);
		*m_currentMatrixStateDirty = true;
	}

	void glPolygonMode (GLenum face, GLenum mode)
	{
		SetRenderStateDirty();
		switch ( face )
		{
		case GL_FRONT:
			m_glPolygonModeFront = mode;
			break;
		case GL_BACK:
			m_glPolygonModeBack = mode;
			break;
		case GL_FRONT_AND_BACK:
			m_glPolygonModeFront = mode;
			m_glPolygonModeBack = mode;
			break;
		default:
			LocalDebugBreak();
			break;
		}
	}

	void glPopMatrix (void)
	{
		SetRenderStateDirty();
		m_currentMatrixStack->Pop();
		*m_currentMatrixStateDirty = true;
	}

	void glPushMatrix (void)
	{
		m_currentMatrixStack->Push();
		// Doesn't dirty matrix state
	}

	void glReadBuffer (GLenum /* mode */)
	{
		// Not that we allow reading from various buffers anyway.
	}

	void glReadPixels (GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels)
	{
		if ( format != GL_RGB || type != GL_UNSIGNED_BYTE) 
		{
			LocalDebugBreak();
			return;
		}
		internalEnd();

#if 0 // Temporarily disable, because I don't want to port DDSURFACEDESC2 to DX8
		if(back) 
		{
			DDSURFACEDESC2 desc = {sizeof(desc) };
			HRESULT hr = back->Lock(NULL, &desc, DDLOCK_READONLY | DDLOCK_WAIT, 0);
			if ( FAILED(hr) ) 
			{
				InterpretError(hr);
				return;
			}
			CopyBitsToRGB(pixels, x, y, width, height, &desc);
			back->Unlock(NULL);
			RELEASENULL(back);
		}
#endif
	}

	static WORD GetNumberOfBits( DWORD dwMask )
	{
		WORD wBits = 0;
		while( dwMask )
		{
			dwMask = dwMask & ( dwMask - 1 );  
			wBits++;
		}
		return wBits;
	}

	static WORD GetShift( DWORD dwMask )
	{
		for(WORD i = 0; i < 32; i++ ) {
			if ( (1 << i) & dwMask ) {
				return i;
			}
		}
		return 0; // no bits in mask.
	}

	// Extract the bits and replicate out to an eight bit value
	static DWORD ExtractAndNormalize(DWORD rgba, DWORD shift, DWORD bits, DWORD mask){
		DWORD v = (rgba & mask) >> shift;
		// Assume bits >= 4
		v = (v | (v << bits));
		v = v >> (bits*2 - 8);
		return v;
	}

#if 0 // Temporarily disable
	void CopyBitsToRGB(void* pixels, DWORD sx, DWORD sy, DWORD width, DWORD height, LPDDSURFACEDESC2 pDesc){
		if ( ! (pDesc->ddpfPixelFormat.dwFlags & DDPF_RGB) ) {
			return; // Can't handle non-RGB surfaces
		}
		// We have to flip the Y axis to convert from D3D to openGL
		long destEndOfLineSkip = -2 * (width * 3);
		unsigned char* pDest = ((unsigned char*) pixels) + (height - 1) * width * 3 ;
		switch ( pDesc->ddpfPixelFormat.dwRGBBitCount ) {
		default:
			return;
		case 16:
			{
				unsigned short* pSource = (unsigned short*)
					(((unsigned char*) pDesc->lpSurface) + sx * sizeof(unsigned short) + sy * pDesc->lPitch);
				DWORD endOfLineSkip = pDesc->lPitch / sizeof(unsigned short) - pDesc->dwWidth;
				DWORD rMask = pDesc->ddpfPixelFormat.dwRBitMask;
				DWORD gMask = pDesc->ddpfPixelFormat.dwGBitMask;
				DWORD bMask = pDesc->ddpfPixelFormat.dwBBitMask;
				DWORD rShift = GetShift(rMask);
				DWORD rBits = GetNumberOfBits(rMask);
				DWORD gShift = GetShift(gMask);
				DWORD gBits = GetNumberOfBits(gMask);
				DWORD bShift = GetShift(bMask);
				DWORD bBits = GetNumberOfBits(bMask);
				for(DWORD y = 0; y < height; y++ ) {
					for (DWORD x = 0; x < width; x++ ) {
						unsigned short rgba = *pSource++;
						*pDest++ = ExtractAndNormalize(rgba, rShift, rBits, rMask);
						*pDest++ = ExtractAndNormalize(rgba, gShift, gBits, gMask);
						*pDest++ = ExtractAndNormalize(rgba, bShift, bBits, bMask);
					}
					pSource += endOfLineSkip;
					pDest += destEndOfLineSkip;
				}
			}
			break;
		case 32:
			{
				unsigned long* pSource = (unsigned long*)
					(((unsigned char*) pDesc->lpSurface) + sx * sizeof(unsigned long) + sy * pDesc->lPitch);
				DWORD endOfLineSkip = pDesc->lPitch / sizeof(unsigned long) - pDesc->dwWidth;
				for(DWORD y = 0; y < height; y++ ) {
					for (DWORD x = 0; x < width; x++ ) {
						unsigned long rgba = *pSource++;
						*pDest++ = RGBA_GETRED(rgba);
						*pDest++ = RGBA_GETGREEN(rgba);
						*pDest++ = RGBA_GETBLUE(rgba);
					}
					pSource += endOfLineSkip;
					pDest += destEndOfLineSkip;
				}
			}
			break;
		}
	}

#endif // Temporarily disable

	inline void glRotatef (GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
	{
		SetRenderStateDirty();
		D3DXMATRIX m;
		D3DXVECTOR3 v;
		v.x = x;
		v.y = y;
		v.z = z;
		// GL uses counterclockwise degrees, DX uses clockwise radians
		float dxAngle = angle * 3.14159265359 / 180;
		m_currentMatrixStack->RotateAxisLocal(&v, dxAngle);
		*m_currentMatrixStateDirty = true;
	}

	inline void glScalef (GLfloat x, GLfloat y, GLfloat z)
	{
		SetRenderStateDirty();
		D3DXMATRIX m;
		D3DXMatrixScaling(&m, x, y, z);
		m_currentMatrixStack->MultMatrixLocal(&m);
		*m_currentMatrixStateDirty = true;
	}

	void glShadeModel (GLenum mode)
	{
		if ( m_glShadeModel != mode )
		{
			SetRenderStateDirty();
			m_glShadeModel = mode;
			m_glShadeModelStateDirty = true;
		}
	}

	inline void glTexCoord2f (GLfloat s, GLfloat t)
	{
		m_OGLPrimitiveVertexBuffer.SetTextureCoord0(s, t);
	}

	void glTexEnvf (GLenum /* target */, GLenum /* pname */, GLfloat param)
	{
		// Ignore target, which must be GL_TEXTURE_ENV
		// Ignore pname, which must be GL_TEXTURE_ENV_MODE
		if ( m_textureState.GetTextEnvMode() != param ) 
		{
#ifdef _XBOX
			{
				static int s_xboxTexEnvLogCount = 0;
				if (s_xboxTexEnvLogCount < 64 || (m_textureState.GetCurrentStage() == 1 && s_xboxTexEnvLogCount < 160))
				{
					XBLF("JA: fakegl texenv stage=%d old=0x%08x new=0x%08x tex=%u enabled=%d",
						m_textureState.GetCurrentStage(),
						(unsigned int)m_textureState.GetTextEnvMode(),
						(unsigned int)param,
						(unsigned int)m_textureState.GetCurrentTexture(),
						m_textureState.GetTexture2D() ? 1 : 0);
					++s_xboxTexEnvLogCount;
				}
			}
#endif
			SetRenderStateDirty();
			m_textureState.SetTextEnvMode(param);
		}
	}

	static int MipMapSize(DWORD width, DWORD height)
	{
		DWORD n = width < height? width : height;
		DWORD result = 1;
		while (n > (DWORD) (1 << result) ) 
		{
			result++;
		}
		return result;
	}

#define LOAD_OURSELVES

	void glTexImage2D (GLenum target, GLint level, GLint internalformat, GLsizei width,
		GLsizei height, GLint /* border */, GLenum format, GLenum type, const GLvoid *pixels)
	{
		HRESULT hr;
		if ( target != GL_TEXTURE_2D || type != GL_UNSIGNED_BYTE) 
		{
			InterpretError(E_FAIL);
			return;
		}

		bool isDynamic = format == GL_LUMINANCE; // Lightmaps use this format.

		DWORD dxWidth = width;
		DWORD dxHeight = height;

		D3DFORMAT srcPixelFormat = GLToDXPixelFormat(internalformat, format);
		D3DFORMAT destPixelFormat = srcPixelFormat;
		// Can the surface handle that format?
		hr = S_OK;
		if ( m_pD3D )
		{
			hr = m_pD3D->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, m_d3dsdBackBuffer.Format,
				0, D3DRTYPE_TEXTURE, destPixelFormat);
		}
		if ( FAILED(hr) ) 
		{
			if ( g_force16bitTextures ) 
			{
				destPixelFormat = D3DFMT_A4R4G4B4;
				hr = m_pD3D ? m_pD3D->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, m_d3dsdBackBuffer.Format,
					0, D3DRTYPE_TEXTURE, destPixelFormat) : S_OK;
				if ( FAILED(hr) ) 
				{
					// Don't know what to do.
					InterpretError(E_FAIL);
					return;
				}
			}
			else 
			{
				destPixelFormat = D3DFMT_A8R8G8B8;
				hr = m_pD3D ? m_pD3D->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, m_d3dsdBackBuffer.Format,
					0, D3DRTYPE_TEXTURE, destPixelFormat) : S_OK;
				if ( FAILED(hr) ) 
				{
					// The card can't handle this pixel format. Switch to D3DX_SF_A4R4G4B4
					destPixelFormat = D3DFMT_A4R4G4B4;
					hr = m_pD3D ? m_pD3D->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, m_d3dsdBackBuffer.Format,
						0, D3DRTYPE_TEXTURE, destPixelFormat) : S_OK;
					if ( FAILED(hr) )
					{
						// Don't know what to do.
						InterpretError(E_FAIL);
						return;
					}
				}
			}
		}

#ifdef LOAD_OURSELVES

		char* goodSizeBits = (char*) pixels;
		if ( dxWidth != (DWORD) width || dxHeight != (DWORD) height )
		{
			// Most likely this is because there is a 256 x 256 limit on the texture size.
			goodSizeBits = new char[sizeof(DWORD) * dxWidth * dxHeight]; 
			DWORD* dest = ((DWORD*) goodSizeBits);
			for ( DWORD y = 0; y < dxHeight; y++) 
			{
				DWORD sy = y * height / dxHeight;
				for(DWORD x = 0; x < dxWidth; x++) 
				{
					DWORD sx = x * width / dxWidth;
					DWORD* source = ((DWORD*) pixels) + sy * dxWidth + sx;
					*dest++ = *source;
				}
			}
			width = dxWidth;
			height = dxHeight;
		}
		// TODO: Convert the pixels on the fly while copying into the DX texture.
		char* compatablePixels;
		DWORD compatablePixelsPitch;

		hr = ConvertToCompatablePixels(internalformat, width, height, format,
				type, destPixelFormat, goodSizeBits, &compatablePixels, &compatablePixelsPitch);

		if ( goodSizeBits != pixels ) 
		{
			delete [] goodSizeBits;
		}
		if ( FAILED(hr))
		{
			InterpretError(hr);
			return;
		}

#endif

		IDirect3DTexture8* pMipMap = m_textures.GetMipMap();
		if ( pMipMap )
		{
			// DX8 textures don't know much. Always reset texture for level zero.
			if ( level == 0 ) 
			{
				m_textures.SetTexture(NULL, D3DFMT_UNKNOWN, 0);
				pMipMap = 0;
			}
			// For non-square textures, OpenGL uses more MIPMAP levels than DirectX does.
			else if ( level >= (int)pMipMap->GetLevelCount() ) 
			{
				return;
			}
		}

		if( ! pMipMap) 
		{
			int levels = 1;
#ifndef _XBOX
			if ( m_hintGenerateMipMaps )
				levels = MipMapSize(width, height);
#endif

			hr = CreateXboxTexture(width, height, levels, 0, destPixelFormat, &pMipMap);
#ifdef _XBOX
			bool ownsTextureHeader = false;
			DWORD registeredTextureBytes = 0;
			if (FAILED(hr) || !pMipMap)
			{
				XBLF("JA: fakegl CreateTexture retry registered tex=%d size=%dx%d levels=%d internal=0x%08x format=0x%08x dest=0x%08x hr=0x%08lx\n",
					m_textures.GetCurrentID(),
					width,
					height,
					levels,
					internalformat,
					format,
					destPixelFormat,
					(unsigned long)hr);
				hr = CreateRegisteredXboxTexture(width, height, levels, 0, destPixelFormat, &pMipMap, &registeredTextureBytes);
				if (SUCCEEDED(hr) && pMipMap)
				{
					ownsTextureHeader = true;
					XBLF("JA: fakegl registered retry succeeded tex=%d ptr=%p bytes=%u\n",
						m_textures.GetCurrentID(), (void*)pMipMap, registeredTextureBytes);
				}
			}
#endif

			if ( FAILED(hr) )
			{
#ifdef _XBOX
				TrackTextureFailure();
				XBLF("JA: fakegl CreateTexture failed tex=%d size=%dx%d levels=%d internal=0x%08x format=0x%08x dest=0x%08x\n",
					m_textures.GetCurrentID(),
					width,
					height,
					levels,
					internalformat,
					format,
					destPixelFormat);
#endif
				if (!UseFallbackTexture(destPixelFormat, internalformat))
				{
					InterpretError(hr);
					return;
				}
#ifdef _XBOX
				XBLF("JA: fakegl using fallback texture tex=%d after allocation failure\n", m_textures.GetCurrentID());
#endif
				return;
			}
			m_textures.SetTexture(pMipMap, destPixelFormat, internalformat
#ifdef _XBOX
				, ownsTextureHeader
#endif
				);
#ifdef _XBOX
			TrackTextureAlloc("rgba", EstimateTextureBytes(destPixelFormat, width, height, levels));
#endif
		}

		glTexSubImage2D_Imp(pMipMap, level, 0, 0, width, height, format, type, compatablePixels, compatablePixelsPitch);

		if ( FAILED(hr) ) 
		{
			InterpretError(hr);
			return;
		}
	}

	bool UploadDDSTexture(GLint internalformat, GLsizei width, GLsizei height,
		GLint mipcount, const GLvoid *pixels, DWORD pixelBytes)
	{
		if (!m_pD3DDev || !pixels || width <= 0 || height <= 0)
		{
			return false;
		}

		D3DFORMAT destPixelFormat = D3DFMT_UNKNOWN;
		switch (internalformat)
		{
			case 0x9995: // GL_DDS1_EXT
				destPixelFormat = D3DFMT_DXT1;
				break;
			case 0x9996: // GL_DDS5_EXT
				destPixelFormat = D3DFMT_DXT5;
				break;
			case 0x9997: // GL_DDS_RGB16_EXT
				destPixelFormat = D3DFMT_R5G6B5;
				break;
			case 0x9998: // GL_DDS_RGBA32_EXT
				destPixelFormat = D3DFMT_A8R8G8B8;
				break;
			default:
				return false;
		}

		int levels = mipcount > 0 ? mipcount : 1;
#ifdef _XBOX
		int availableLevels = DDSAvailableLevels(destPixelFormat, (DWORD)width, (DWORD)height, pixelBytes);
		if (availableLevels <= 0)
		{
			XBLF("JA: fakegl DDS upload rejected tex=%d size=%dx%d internal=0x%08x bytes=%u no complete levels\n",
				m_textures.GetCurrentID(), width, height, internalformat, pixelBytes);
			return false;
		}
		if (levels > availableLevels)
		{
			XBLF("JA: fakegl DDS mip clamp tex=%d size=%dx%d requested=%d available=%d bytes=%u\n",
				m_textures.GetCurrentID(), width, height, levels, availableLevels, pixelBytes);
			levels = availableLevels;
		}
		const int xboxDdsMaxDim = 1024;
		const BYTE* ddsStart = (const BYTE*)pixels;
		DWORD ddsBytes = pixelBytes;
		int skippedTopMips = 0;
		while ((width > xboxDdsMaxDim || height > xboxDdsMaxDim) && levels > 1)
		{
			DWORD topBytes = DDSLevelRowBytes(destPixelFormat, (DWORD)width) * DDSLevelRows(destPixelFormat, (DWORD)height);
			if (!topBytes || ddsBytes <= topBytes)
				break;
			ddsStart += topBytes;
			ddsBytes -= topBytes;
			if (width > 1)
				width >>= 1;
			if (height > 1)
				height >>= 1;
			--levels;
			++skippedTopMips;
		}
		if (skippedTopMips)
		{
			XBLF("JA: fakegl DDS top-mip skip tex=%d skipped=%d newSize=%dx%d levels=%d bytes=%u\n",
				m_textures.GetCurrentID(), skippedTopMips, width, height, levels, ddsBytes);
		}
		if ((width > xboxDdsMaxDim || height > xboxDdsMaxDim) && levels == 1)
		{
			// Some DDS assets have only one stored level, so there is no lower mip to skip.
			// Use the first compressed blocks in a smaller texture to avoid late Cxbx allocation failure.
			GLsizei oldWidth = width;
			GLsizei oldHeight = height;
			while (width > xboxDdsMaxDim && width > 1)
				width >>= 1;
			while (height > xboxDdsMaxDim && height > 1)
				height >>= 1;
			XBLF("JA: fakegl DDS single-mip cap tex=%d oldSize=%dx%d newSize=%dx%d bytes=%u\n",
				m_textures.GetCurrentID(), oldWidth, oldHeight, width, height, ddsBytes);
		}
#endif
		IDirect3DTexture8* pMipMap = NULL;
		bool ownsTextureHeader = false;
		DWORD registeredTextureBytes = 0;
		void* registeredTextureData = NULL;
#ifdef _XBOX
		static int s_ddsDetailLogs = 0;
		const bool logDdsDetail = (s_ddsDetailLogs < 96);
		if (logDdsDetail)
		{
			XBLF("JA: fakegl DDS CreateTexture pre tex=%d size=%dx%d levels=%d dest=0x%08x bytes=%u registered=1",
				m_textures.GetCurrentID(), width, height, levels,
				(unsigned int)destPixelFormat, pixelBytes);
		}
		HRESULT hr = CreateRegisteredXboxTexture(width, height, levels, 0, destPixelFormat, &pMipMap, &registeredTextureBytes, &registeredTextureData);
		if (SUCCEEDED(hr) && pMipMap)
		{
			ownsTextureHeader = true;
		}
		else
		{
			XBLF("JA: fakegl DDS registered texture failed tex=%d hr=0x%08lx; retry CreateTexture2\n",
				m_textures.GetCurrentID(), (unsigned long)hr);
			hr = CreateXboxTexture(width, height, levels, 0, destPixelFormat, &pMipMap);
		}
#else
		HRESULT hr = CreateXboxTexture(width, height, levels, 0, destPixelFormat, &pMipMap);
#endif
#ifdef _XBOX
		if (logDdsDetail)
		{
			XBLF("JA: fakegl DDS CreateTexture post tex=%d hr=0x%08lx ptr=%p registered=%d allocBytes=%u",
				m_textures.GetCurrentID(), (unsigned long)hr, (void*)pMipMap,
				ownsTextureHeader ? 1 : 0, registeredTextureBytes);
		}
#endif
		if (FAILED(hr) || !pMipMap)
		{
#ifdef _XBOX
			TrackTextureFailure();
			XBLF("JA: fakegl DDS CreateTexture failed tex=%d size=%dx%d levels=%d internal=0x%08x dest=0x%08x bytes=%u\n",
				m_textures.GetCurrentID(), width, height, levels, internalformat,
				destPixelFormat, pixelBytes);
			if (UseFallbackTexture(destPixelFormat, internalformat))
			{
				XBLF("JA: fakegl DDS using fallback texture tex=%d after allocation failure\n",
					m_textures.GetCurrentID());
				return true;
			}
#endif
			return false;
		}

		const BYTE* src = (const BYTE*)pixels;
		DWORD remaining = pixelBytes;
#ifdef _XBOX
		src = ddsStart;
		remaining = ddsBytes;
		if (ownsTextureHeader && registeredTextureData &&
			(destPixelFormat == D3DFMT_DXT1 ||
			 destPixelFormat == D3DFMT_DXT3 ||
			 destPixelFormat == D3DFMT_DXT5))
		{
			DWORD copyBytes = remaining;
			if (registeredTextureBytes && copyBytes > registeredTextureBytes)
				copyBytes = registeredTextureBytes;
			memcpy(registeredTextureData, src, copyBytes);
			m_textures.SetTexture(pMipMap, destPixelFormat, internalformat, ownsTextureHeader);
			m_textureState.DirtyTexture(m_textures.GetCurrentID());
			if (logDdsDetail)
			{
				XBLF("JA: fakegl DDS registered direct copy tex=%d bytes=%u registeredBytes=%u ptr=%p",
					m_textures.GetCurrentID(), copyBytes, registeredTextureBytes, registeredTextureData);
				++s_ddsDetailLogs;
			}
			TrackTextureAlloc("dds", copyBytes);
			return true;
		}
#endif
		DWORD levelWidth = (DWORD)width;
		DWORD levelHeight = (DWORD)height;
		bool ok = true;

		for (int level = 0; level < levels; ++level)
		{
			D3DSURFACE_DESC desc;
#ifdef _XBOX
			if (logDdsDetail)
			{
				XBLF("JA: fakegl DDS GetLevelDesc pre tex=%d level=%d",
					m_textures.GetCurrentID(), level);
			}
#endif
			hr = pMipMap->GetLevelDesc(level, &desc);
#ifdef _XBOX
			if (logDdsDetail)
			{
				XBLF("JA: fakegl DDS GetLevelDesc post tex=%d level=%d hr=0x%08lx size=%ux%u fmt=0x%08x",
					m_textures.GetCurrentID(), level, (unsigned long)hr,
					(unsigned int)desc.Width, (unsigned int)desc.Height,
					(unsigned int)desc.Format);
			}
#endif
			if (FAILED(hr))
			{
				ok = false;
				break;
			}

			DWORD rowBytes = DDSLevelRowBytes(destPixelFormat, levelWidth);
			DWORD rows = DDSLevelRows(destPixelFormat, levelHeight);
			DWORD levelBytes = rowBytes * rows;
			if (remaining < levelBytes)
			{
#ifdef _XBOX
				XBLF("JA: fakegl DDS upload truncated tex=%d level=%d need=%u remaining=%u\n",
					m_textures.GetCurrentID(), level, levelBytes, remaining);
#endif
				ok = false;
				break;
			}

			D3DLOCKED_RECT lockedRect;
#ifdef _XBOX
			if (logDdsDetail)
			{
				XBLF("JA: fakegl DDS LockRect pre tex=%d level=%d rowBytes=%u rows=%u levelBytes=%u remaining=%u",
					m_textures.GetCurrentID(), level, rowBytes, rows, levelBytes, remaining);
			}
#endif
			hr = pMipMap->LockRect(level, &lockedRect, NULL, 0);
#ifdef _XBOX
			if (logDdsDetail)
			{
				XBLF("JA: fakegl DDS LockRect post tex=%d level=%d hr=0x%08lx bits=%p pitch=%ld",
					m_textures.GetCurrentID(), level, (unsigned long)hr,
					lockedRect.pBits, (long)lockedRect.Pitch);
			}
#endif
			if (FAILED(hr))
			{
#ifdef _XBOX
				XBLF("JA: fakegl DDS LockRect failed tex=%d level=%d size=%ux%u format=0x%08x\n",
					m_textures.GetCurrentID(), level, desc.Width, desc.Height, desc.Format);
#endif
				ok = false;
				break;
			}

			if (destPixelFormat == D3DFMT_DXT1 ||
				destPixelFormat == D3DFMT_DXT3 ||
				destPixelFormat == D3DFMT_DXT5)
			{
				const BYTE* sp = src;
				BYTE* dp = (BYTE*)lockedRect.pBits;
				for (DWORD y = 0; y < rows; ++y)
				{
					memcpy(dp, sp, rowBytes);
					sp += rowBytes;
					dp += lockedRect.Pitch;
				}
			}
			else
			{
				XGSwizzleRect(src, rowBytes, NULL, lockedRect.pBits,
					levelWidth, levelHeight, NULL, BytesPerPixel(destPixelFormat));
			}

#ifdef _XBOX
			if (logDdsDetail)
			{
				XBLF("JA: fakegl DDS UnlockRect pre tex=%d level=%d",
					m_textures.GetCurrentID(), level);
			}
#endif
			pMipMap->UnlockRect(level);
#ifdef _XBOX
			if (logDdsDetail)
			{
				XBLF("JA: fakegl DDS UnlockRect post tex=%d level=%d",
					m_textures.GetCurrentID(), level);
			}
#endif
			src += levelBytes;
			remaining -= levelBytes;
			if (levelWidth > 1)
				levelWidth >>= 1;
			if (levelHeight > 1)
				levelHeight >>= 1;
		}

		if (!ok)
		{
#ifdef _XBOX
			if (ownsTextureHeader)
			{
				delete (D3DTexture*)pMipMap;
			}
			else
#endif
			{
				pMipMap->Release();
			}
			return false;
		}

		m_textures.SetTexture(pMipMap, destPixelFormat, internalformat
#ifdef _XBOX
			, ownsTextureHeader
#endif
			);
		m_textureState.DirtyTexture(m_textures.GetCurrentID());
#ifdef _XBOX
		if (logDdsDetail)
		{
			XBLF("JA: fakegl DDS SetTextureEntry tex=%d ptr=%p",
				m_textures.GetCurrentID(), (void*)pMipMap);
			++s_ddsDetailLogs;
		}
		TrackTextureAlloc("dds", pixelBytes);
		static int s_ddsUploadLogs = 0;
		if (s_ddsUploadLogs < 96)
		{
			XBLF("JA: fakegl DDS upload tex=%d size=%dx%d levels=%d internal=0x%08x dest=0x%08x bytes=%u\n",
				m_textures.GetCurrentID(), width, height, levels, internalformat,
				destPixelFormat, pixelBytes);
			++s_ddsUploadLogs;
		}
#endif
		return true;
	}

	void glTexParameterf (GLenum target, GLenum pname, GLfloat param)
	{
		switch(target)
		{
		case GL_TEXTURE_2D:
			{
				SetRenderStateDirty();
				TextureEntry* current = m_textures.GetCurrentEntry();
				m_textureState.DirtyTexture(m_textures.GetCurrentID());
				
				switch(pname)
				{
				case GL_TEXTURE_MIN_FILTER:
					current->m_glTexParameter2DMinFilter = param;
					break;
				case GL_TEXTURE_MAG_FILTER:
					current->m_glTexParameter2DMagFilter = param;
					break;
				case GL_TEXTURE_WRAP_S:
					current->m_glTexParameter2DWrapS = param;
					break;
				case GL_TEXTURE_WRAP_T:
					current->m_glTexParameter2DWrapT = param;
					break;
				case D3D_TEXTURE_MAXANISOTROPY:
					current->m_maxAnisotropy = param;
					break;
				default:
					LocalDebugBreak();
				}
			}
			break;
		default:
			LocalDebugBreak();
			break;
		}
	}

	void glTexSubImage2D (GLenum target, GLint level,
		GLint xoffset, GLint yoffset, GLsizei width, GLsizei height,
		GLenum format, GLenum type, const GLvoid *pixels)
	{
		if ( target != GL_TEXTURE_2D ) 
		{
			LocalDebugBreak();
			return;
		}

		if ( width <= 0 || height <= 0 )
			return;

		IDirect3DTexture8* pTexture = m_textures.GetMipMap();
		if ( ! pTexture ) 
			return;

		internalEnd(); // We may have a pending drawing using the old texture state.

		// To do: Convert the pixels on the fly while copying into the DX texture.

		char* compatablePixels = 0;
		DWORD compatablePixelsPitch;
		if ( FAILED(ConvertToCompatablePixels(m_textures.GetInternalFormat(),
				width, height,
				format, type, m_textures.GetSurfaceFormat(),
				pixels, &compatablePixels, &compatablePixelsPitch))) 
		{
			LocalDebugBreak();
			return;
		}

		glTexSubImage2D_Imp(pTexture, level, xoffset, yoffset, width, height, format, type, compatablePixels, compatablePixelsPitch);
	}

	char* StickyAlloc(DWORD size)
	{
		if ( m_stickyAllocSize < size ) 
		{
			delete [] m_stickyAlloc;
			m_stickyAlloc = new char[size];
			m_stickyAllocSize = size;
		}
		return m_stickyAlloc;
	}

// Slower than just locking and unlocking. But both are really slow on NVIDIA hardware, due
// to texture swizzleing / unswizzleing.
// #define USE_IMAGE_SURFACE

	void glTexSubImage2D_Imp (IDirect3DTexture8* pMipMap, GLint level,
		GLint xoffset, GLint yoffset, GLsizei width, GLsizei height,
		GLenum /* format */, GLenum /* type */, const char* compatablePixels, int compatablePixelsPitch)
	{
		HRESULT hr = S_OK;
		D3DLOCKED_RECT lockedRect;
		D3DSURFACE_DESC desc;

		hr = pMipMap->GetLevelDesc(level, &desc);
		if ( FAILED(hr) )
		{
			InterpretError(hr);
			return;
		}

		if ( xoffset == 0 && yoffset == 0 && width == (GLsizei)desc.Width && height == (GLsizei)desc.Height )
		{
			D3DLOCKED_RECT swizzleRect;
			hr = pMipMap->LockRect(level, &swizzleRect, NULL, 0);
			if ( FAILED(hr) )
			{
#ifdef _XBOX
				XBLF("JA: fakegl direct swizzle LockRect failed tex=%d level=%d size=%dx%d format=0x%08x\n",
					m_textures.GetCurrentID(),
					level,
					desc.Width,
					desc.Height,
					desc.Format);
#endif
				InterpretError(hr);
				return;
			}

			XGSwizzleRect(
					compatablePixels,
					compatablePixelsPitch,
					NULL,
					swizzleRect.pBits,
					desc.Width,
					desc.Height,
					NULL,
					BytesPerPixel(desc.Format));

			pMipMap->UnlockRect(level);
			return;
		}

//#define TRY_REGION
#ifdef TRY_REGION
		RECT lockRect;
		lockRect.top = yoffset;
		lockRect.left = xoffset;
		lockRect.bottom = yoffset + height;
		lockRect.right = xoffset + width;
		hr = pMipMap->LockRect(level, &lockedRect, &lockRect, 0);
#else
		hr = pMipMap->LockRect(level, &lockedRect, NULL, 0);
#endif
		if ( FAILED(hr) ) 
			InterpretError(hr);
		else 
		{
			const char* sp = compatablePixels;
			char* dp = (char*) lockedRect.pBits + yoffset * lockedRect.Pitch;

			if ( compatablePixelsPitch > lockedRect.Pitch )
				LocalDebugBreak();

			if ( compatablePixelsPitch != lockedRect.Pitch ) 
			{
				for(int i = 0; i < height; i++ ) 
				{
					memcpy(dp, sp, compatablePixelsPitch);
					sp += compatablePixelsPitch;
					dp += lockedRect.Pitch;
				}
			}
			else 
			{
				memcpy(dp, sp, compatablePixelsPitch * height);
			}
			pMipMap->UnlockRect(level);

			//At this point we have a complete pMipMap, ready to be swizzled
			D3DSurface *surface;
			D3DSurface *surfaceTemp;

			RECT rect;
			
#ifdef TRY_REGION
			rect.left = xoffset;
        	rect.top = yoffset;
        	rect.right = xoffset + width;
        	rect.bottom = yoffset + height;
#else
			rect.left = 0;
        	rect.top = 0;
        	rect.right = width;
        	rect.bottom = height;
#endif
			// Create temporary surface for partial updates only.
			hr = m_pD3DDev->CreateImageSurface(desc.Width, desc.Height, desc.Format, &surfaceTemp);
			if ( FAILED(hr) || !surfaceTemp )
			{
#ifdef _XBOX
				XBLF("JA: fakegl CreateImageSurface failed tex=%d level=%d update=%dx%d at %d,%d surface=%dx%d format=0x%08x\n",
					m_textures.GetCurrentID(),
					level,
					width,
					height,
					xoffset,
					yoffset,
					desc.Width,
					desc.Height,
					desc.Format);
#endif
				InterpretError(hr);
				return;
			}

			// Lock the texture
			D3DLOCKED_RECT lr, lr2;
        	hr = pMipMap->LockRect( level, &lr, &rect, 0 );
			if ( FAILED(hr) )
			{
				surfaceTemp->Release();
				InterpretError(hr);
				return;
			}

			// Go down to surface level
			hr = pMipMap->GetSurfaceLevel(level, &surface);
			if ( FAILED(hr) || !surface )
			{
				pMipMap->UnlockRect(level);
				surfaceTemp->Release();
				InterpretError(hr);
				return;
			}

			// Copy surf to temp surf
			hr = D3DXLoadSurfaceFromSurface(surfaceTemp, NULL, NULL, surface, NULL, NULL, D3DX_FILTER_NONE, NULL);
			if ( FAILED(hr) )
			{
				surface->Release();
				pMipMap->UnlockRect(level);
				surfaceTemp->Release();
				InterpretError(hr);
				return;
			}

			hr = surfaceTemp->LockRect(&lr2, NULL, NULL);
			if ( FAILED(hr) )
			{
				surface->Release();
				pMipMap->UnlockRect(level);
				surfaceTemp->Release();
				InterpretError(hr);
				return;
			}

			// Check the formats and change bytesPerPixel accordingly

			switch ( desc.Format ) 
			{
				case D3DFMT_P8:
				
				case D3DFMT_AL8:
				case D3DFMT_A8:
				case D3DFMT_L8: // FIXME: Try workaround? -> convert to X8R8G8B8 / A8R8G8B8 ???
					bytes = 1;
				break;

				case D3DFMT_R5G6B5:
				case D3DFMT_A4R4G4B4:
				case D3DFMT_A8L8:
					
					bytes = 2;
				break;

				case D3DFMT_A8R8G8B8:
				case D3DFMT_X8R8G8B8:
					bytes = 4;
				break;

				default:
					// This really should not happen, as it's a format we do not know yet
					bytes = 4;
					LocalDebugBreak();
				break;
			}

			/*
			char buf[100];
			sprintf(buf,"TEXFORMAT 0x%08x\n", desc.Format);
			OutputDebugString(buf);

			sprintf(buf,"MIP LEVEL %d\n", level);
			OutputDebugString(buf);
			*/

			// XBox textures need to be swizzled
			XGSwizzleRect(
					lr2.pBits,		// pSource, 
					lr2.Pitch,		// Pitch,
					NULL,			// pRect,
					lr.pBits,		// pDest,
					desc.Width,		// Width,
					desc.Height,	// Height,
					NULL,			// pPoint,
					bytes);			// BytesPerPixel

			surfaceTemp->UnlockRect();
			surfaceTemp->Release();
			surface->Release();
			pMipMap->UnlockRect(level);
		}
	}

	inline void glTranslatef (GLfloat x, GLfloat y, GLfloat z)
	{
		SetRenderStateDirty();
		D3DXMATRIX m;
		D3DXMatrixTranslation(&m, x, y, z);
		m_currentMatrixStack->MultMatrixLocal(&m);
		*m_currentMatrixStateDirty = true;
	}

	inline void glVertex2f (GLfloat x, GLfloat y)
	{
		m_OGLPrimitiveVertexBuffer.SetVertex(x, y, 0);
	}

	inline void glVertex3f (GLfloat x, GLfloat y, GLfloat z)
	{
		m_OGLPrimitiveVertexBuffer.SetVertex(x, y, z);
	}

	inline void glVertex3fv (const GLfloat *v)
	{
		m_OGLPrimitiveVertexBuffer.SetVertex(v[0], v[1], v[2]);
	}

	void glViewport (GLint x, GLint y, GLsizei width, GLsizei height)
	{
		if ( m_glViewPortX != x || m_glViewPortY != y ||
			m_glViewPortWidth != width || m_glViewPortHeight != height ) 
		{
			SetRenderStateDirty();
			m_glViewPortX = x;
			m_glViewPortY = y;
			m_glViewPortWidth = width;
			m_glViewPortHeight = height;

			m_bViewPortDirty = true;
		}
	}

	void SwapBuffers()
	{
#ifdef _XBOX
		static int s_xboxSwapLogCount = 0;
		const bool logSwap = (s_xboxSwapLogCount < 8 || ((s_xboxSwapLogCount & 255) == 0));
		if (logSwap)
		{
			XBLF("JA: fakegl SwapBuffers #%d enter dev=%p needBeginScene=%d",
				s_xboxSwapLogCount, (void*)m_pD3DDev, (int)m_needBeginScene);
		}
#endif
		internalEnd();
		if (!m_pD3DDev)
		{
#ifdef _XBOX
			XBLog_Write("JA: fakegl SwapBuffers skipped because m_pD3DDev is NULL");
			s_xboxSwapLogCount++;
#endif
			return;
		}
#ifdef _XBOX
		if (logSwap)
		{
			XBLF("JA: fakegl SwapBuffers #%d pre-EndScene KickPushBuffer", s_xboxSwapLogCount);
		}
		m_pD3DDev->KickPushBuffer();
		Sleep(0);
#endif
		HRESULT hrEndScene = m_pD3DDev->EndScene();
#ifdef _XBOX
		if (logSwap)
		{
			XBLF("JA: fakegl SwapBuffers #%d EndScene hr=0x%08lx", s_xboxSwapLogCount, (unsigned long)hrEndScene);
		}
		if (logSwap)
		{
			XBLF("JA: fakegl SwapBuffers #%d pre-Present BlockUntilIdle", s_xboxSwapLogCount);
		}
		m_pD3DDev->KickPushBuffer();
		m_pD3DDev->BlockUntilIdle();
		Sleep(0);
		if (logSwap)
		{
			XBLF("JA: fakegl SwapBuffers #%d pre-Present idle complete", s_xboxSwapLogCount);
		}
#endif
		m_needBeginScene = true;
#if 0
		static int frameCounter;
		frameCounter++;
		char buf[100];
		sprintf(buf, "Present %d\n", frameCounter);
		OutputDebugString(buf);
#endif


#if 0 //PROFILE
#define MB	(1024*1024)
#define AddStr(a,b) (pstrOut += wsprintf( pstrOut, a, b ))

		MEMORYSTATUS stat;
		CHAR strOut[1024], *pstrOut;

		// Get the memory status.
		GlobalMemoryStatus( &stat );

		// Setup the output string.
		pstrOut = strOut;
		AddStr( "%4d total MB of virtual memory.\n", stat.dwTotalVirtual / MB );
		AddStr( "%4d  free MB of virtual memory.\n", stat.dwAvailVirtual / MB );
		AddStr( "%4d total MB of physical memory.\n", stat.dwTotalPhys / MB );
		AddStr( "%4d  free MB of physical memory.\n", stat.dwAvailPhys / MB );
		AddStr( "%4d total MB of paging file.\n", stat.dwTotalPageFile / MB );
		AddStr( "%4d  free MB of paging file.\n", stat.dwAvailPageFile / MB );
		AddStr( "%4d  percent of memory is in use.\n", stat.dwMemoryLoad );

		// Output the string.
		OutputDebugString( strOut );
#endif

#ifdef PROFILE
D3DPERF_SetShowFrameRateInterval( 1000 );
#endif

#ifdef _XBOX
		if (logSwap)
		{
			XBLF("JA: fakegl SwapBuffers #%d Present...", s_xboxSwapLogCount);
		}
#endif
        HRESULT hrPresent = m_pD3DDev->Present(NULL, NULL, NULL, NULL);
#ifdef _XBOX
		if (logSwap)
		{
			XBLF("JA: fakegl SwapBuffers #%d Present hr=0x%08lx", s_xboxSwapLogCount, (unsigned long)hrPresent);
		}
		s_xboxSwapLogCount++;
#endif
#if 0
		if ( frameCounter == 3 )
		{
			Sleep(1700);
			LocalDebugBreak();
		}
#endif
	}

	void SetGammaRamp(const unsigned char* gammaTable)
	{
		D3DGAMMARAMP gammaRamp;
		for(int i = 0; i < 256; i++ ) 
		{
			WORD value = gammaTable[i];
			value = value + (value << 8); // * 257
			gammaRamp.red[i] = value;
			gammaRamp.green[i] = value;
			gammaRamp.blue[i] = value;
		}
		m_pD3DDev->SetGammaRamp(D3DSGR_NO_CALIBRATION, &gammaRamp);
	}

	void Hint_GenerateMipMaps(int value)
	{
		m_hintGenerateMipMaps = value != 0;
	}

	void EvictTextures()
	{
		// MARTY - Not available on the XBox!
		//m_pD3DDev->ResourceManagerDiscardBytes(0);
	}
private:

	void SetRenderStateDirty()
	{
		if ( ! m_glRenderStateDirty )
		{
			internalEnd();
			m_glRenderStateDirty = true;
		}
	}

	HRESULT HandleWindowedModeChanges()
	{
		return S_OK;
	}

	void SetGLRenderState()
	{
		if ( ! m_glRenderStateDirty )
		{
			return;
		}
		m_glRenderStateDirty = false;
		HRESULT hr;
		if ( m_glAlphaStateDirty )
		{
			m_glAlphaStateDirty = false;
			// Alpha test
			m_pD3DDev->SetRenderState( D3DRS_ALPHATESTENABLE,
				m_glAlphaTest ? TRUE : FALSE );
			m_pD3DDev->SetRenderState(D3DRS_ALPHAFUNC,
				m_glAlphaTest ? GLToDXCompare(m_glAlphaFunc) : D3DCMP_ALWAYS);
			m_pD3DDev->SetRenderState(D3DRS_ALPHAREF, 255 * m_glAlphaFuncRef);
		}
		if ( m_glBlendStateDirty )
		{
			m_glBlendStateDirty = false;
			// Alpha blending
			DWORD srcBlend = m_glBlend ? GLToDXSBlend(m_glBlendFuncSFactor) : D3DBLEND_ONE;
			DWORD destBlend = m_glBlend ? GLToDXDBlend(m_glBlendFuncDFactor) : D3DBLEND_ZERO;
			m_pD3DDev->SetRenderState( D3DRS_SRCBLEND,  srcBlend );
			m_pD3DDev->SetRenderState( D3DRS_DESTBLEND, destBlend );
			m_pD3DDev->SetRenderState( D3DRS_ALPHABLENDENABLE, m_glBlend ? TRUE : FALSE );
		}
		if ( m_glCullStateDirty ) 
		{
			m_glCullStateDirty = false;
			D3DCULL cull = D3DCULL_NONE;
			if ( m_glCullFace ) 
			{
				switch(m_glCullFaceMode)
				{
				default:
				case GL_BACK:
					cull = D3DCULL_CW;
					break;
				case GL_FRONT:
					cull = D3DCULL_CCW;
					break;
				}
			}
			hr = m_pD3DDev->SetRenderState(D3DRS_CULLMODE, cull);
			if ( FAILED(hr) ){
				InterpretError(hr);
			}
		}
		if ( m_glShadeModelStateDirty )
		{
			m_glShadeModelStateDirty = false;
			// Shade model
			m_pD3DDev->SetRenderState( D3DRS_SHADEMODE, 
				m_glShadeModel == GL_SMOOTH ? D3DSHADE_GOURAUD : D3DSHADE_FLAT );
		}
			
		{
			m_textureState.SetTextureStageState(m_pD3DDev, &m_textures);
		}

		if ( m_glDepthStateDirty ) 
		{
			m_glDepthStateDirty = false;
			m_pD3DDev->SetRenderState( D3DRS_ZENABLE, m_glDepthTest ? D3DZB_TRUE : D3DZB_FALSE);
			m_pD3DDev->SetRenderState( D3DRS_ZWRITEENABLE, m_glDepthMask ? TRUE : FALSE);
			DWORD zfunc = GLToDXCompare(m_glDepthFunc);
			m_pD3DDev->SetRenderState( D3DRS_ZFUNC, zfunc );
		}
		if ( m_glFogStateDirty )
		{
			m_glFogStateDirty = false;
			m_pD3DDev->SetRenderState( D3DRS_FOGENABLE, m_glFog ? TRUE : FALSE );
			m_pD3DDev->SetRenderState( D3DRS_FOGTABLEMODE, GLToDXFogMode(m_glFogMode) );
			m_pD3DDev->SetRenderState( D3DRS_FOGDENSITY, *(DWORD*)&m_glFogDensity );
			m_pD3DDev->SetRenderState( D3DRS_FOGSTART, *(DWORD*)&m_glFogStart );
			m_pD3DDev->SetRenderState( D3DRS_FOGEND, *(DWORD*)&m_glFogEnd );
			m_pD3DDev->SetRenderState( D3DRS_FOGCOLOR, m_glFogColor );
#ifdef _XBOX
			{
				static int s_fogApplyLogCount = 0;
				if (s_fogApplyLogCount < 24)
				{
					XBLF("JA: fakegl fog apply enabled=%d mode=0x%04x start=%g end=%g density=%g color=0x%08x",
						m_glFog ? 1 : 0,
						(unsigned int)m_glFogMode,
						(float)m_glFogStart,
						(float)m_glFogEnd,
						(float)m_glFogDensity,
						(unsigned int)m_glFogColor);
				}
				++s_fogApplyLogCount;
			}
#endif
		}
		if ( m_modelViewMatrixStateDirty ) 
		{
			m_modelViewMatrixStateDirty = false;
			m_pD3DDev->SetTransform( D3DTS_WORLD, m_modelViewMatrixStack->GetTop() );
		}
		if ( m_viewMatrixStateDirty ) 
		{
			m_viewMatrixStateDirty = false;
			m_pD3DDev->SetTransform( D3DTS_VIEW, & m_d3dViewMatrix );
		}
		if ( m_projectionMatrixStateDirty ) 
		{
			m_projectionMatrixStateDirty = false;
			m_pD3DDev->SetTransform( D3DTS_PROJECTION, m_projectionMatrixStack->GetTop() );
		}
		if ( m_glClipPlane0StateDirty )
		{
			m_glClipPlane0StateDirty = false;
#ifdef _XBOX
			static int s_clipPlaneApplyLogCount = 0;
			if (s_clipPlaneApplyLogCount < 16)
			{
				XBLF("JA: fakegl GL_CLIP_PLANE0 apply skipped on Xbox fixed-function path enabled=%d plane=%g,%g,%g,%g",
					m_glClipPlane0Enabled ? 1 : 0,
					m_glClipPlane0[0],
					m_glClipPlane0[1],
					m_glClipPlane0[2],
					m_glClipPlane0[3]);
			}
			++s_clipPlaneApplyLogCount;
#else
			m_pD3DDev->SetClipPlane( 0, m_glClipPlane0 );
			m_pD3DDev->SetRenderState( D3DRS_CLIPPLANEENABLE,
				m_glClipPlane0Enabled ? 1 : 0 );
#endif
		}
		if ( m_textureMatrixStateDirty )
		{
			m_textureMatrixStateDirty = false;
			m_pD3DDev->SetTransform( D3DTS_TEXTURE0, m_textureMatrixStack->GetTop() );
		}
		if ( m_bViewPortDirty )
		{
			m_bViewPortDirty = false;
			D3DVIEWPORT8 viewData;

			viewData.X = 0;//m_glViewPortX;
			viewData.Y = 0;//m_windowHeight - (m_glViewPortY + m_glViewPortHeight);
			viewData.Width  = m_glViewPortWidth;
			viewData.Height = m_glViewPortHeight;
			viewData.MinZ = m_glDepthRangeNear;     
			viewData.MaxZ = m_glDepthRangeFar;
#ifdef _XBOX
			{
				static int s_xboxViewportApplyLogBudget = 96;
				if (s_xboxViewportApplyLogBudget > 0)
				{
					XBLF("JA: fakegl SetViewport requested=%d,%d %dx%d applied=%lu,%lu %lux%lu z=%g..%g",
						m_glViewPortX,
						m_glViewPortY,
						m_glViewPortWidth,
						m_glViewPortHeight,
						(unsigned long)viewData.X,
						(unsigned long)viewData.Y,
						(unsigned long)viewData.Width,
						(unsigned long)viewData.Height,
						(float)viewData.MinZ,
						(float)viewData.MaxZ);
					--s_xboxViewportApplyLogBudget;
				}
			}
#endif
			m_pD3DDev->SetViewport(&viewData);
		}
	}

	void EnsureDriverInfo() 
	{
		if ( ! m_vendor ) 
		{
			if ( m_pD3D )
			{
				m_pD3D->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &m_dddi);
				m_vendor = m_dddi.Driver;
				m_renderer = m_dddi.Description;
				wsprintf(m_version, "%u.%u.%u.%u %u.%u.%u.%u %u", 
					HIWORD(m_dddi.DriverVersion.HighPart),
					LOWORD(m_dddi.DriverVersion.HighPart),
					HIWORD(m_dddi.DriverVersion.LowPart),
					LOWORD(m_dddi.DriverVersion.LowPart),
					m_dddi.VendorId,
					m_dddi.DeviceId,
					m_dddi.SubSysId,
					m_dddi.Revision,
					m_dddi.WHQLLevel
					);
			}
			else
			{
				m_vendor = "Xbox driver";
				m_renderer = "Xbox NV2A";
				lstrcpy(m_version, "8.0");
			}
			if ( m_textureState.GetMaxStages() > 1 ) 
			{
				m_extensions = " GL_SGIS_multitexture GL_EXT_texture_object ";
			}
			else 
			{
				m_extensions = " GL_EXT_texture_object ";
			}
		}
	}

	D3DFORMAT GLToDXPixelFormat(GLint internalformat, GLenum format)
	{
		D3DFORMAT d3dFormat = D3DFMT_UNKNOWN;
		if ( g_force16bitTextures ) 
		{
			switch ( format ) 
			{
			case GL_RGBA:
				switch ( internalformat ) 
				{
				default:
				case 4:
//					d3dFormat = D3DFMT_A1R5G5B5; break;
					d3dFormat = D3DFMT_A4R4G4B4; break;
				case 3:
					d3dFormat = D3DFMT_R5G6B5; break;
				}
				break;
			case GL_COLOR_INDEX: d3dFormat = D3DFMT_P8; break;
			case GL_LUMINANCE: d3dFormat = D3DFMT_L8; break;
			case GL_ALPHA: d3dFormat = D3DFMT_A8; break;
			case GL_INTENSITY: d3dFormat = D3DFMT_L8; break;
			case GL_RGBA4: d3dFormat = D3DFMT_A4R4G4B4; break;
			default:
				InterpretError(E_FAIL);
			}
		}
		else 
		{
			// for
			switch ( format ) 
			{
			case GL_RGBA:
				switch ( internalformat ) 
				{
				default:
				case 4:
					d3dFormat = D3DFMT_A8R8G8B8; break;
				case 3:
					d3dFormat = D3DFMT_X8R8G8B8; break;
				}
				break;
			case GL_COLOR_INDEX: d3dFormat = D3DFMT_P8; break;
			case GL_LUMINANCE: d3dFormat = D3DFMT_L8; break;
			case GL_ALPHA: d3dFormat = D3DFMT_A8; break;
			case GL_INTENSITY: d3dFormat = D3DFMT_L8; break;
			case GL_RGBA4: d3dFormat = D3DFMT_A4R4G4B4; break;
			default:
				InterpretError(E_FAIL);
			}
		}
		return d3dFormat;
	}

// Avoid warning 4061, enumerant 'foo' in switch of enum 'bar' is not explicitly handled by a case label.
#pragma warning( push )
#pragma warning( disable : 4061)

		HRESULT ConvertToCompatablePixels(GLint internalformat,
		GLsizei width, GLsizei height,
		GLenum /* format */, GLenum type,
		D3DFORMAT dxPixelFormat,
		const GLvoid *pixels, char**  compatablePixels,
		DWORD* newPitch){
		HRESULT hr = S_OK;
		if ( type != GL_UNSIGNED_BYTE ) 
		{
			return E_FAIL;
		}
		switch ( dxPixelFormat )
		{
		default:
			LocalDebugBreak();
			break;
		case D3DFMT_P8:
		case D3DFMT_L8:
		case D3DFMT_A8:
			{
				char* copy = StickyAlloc(width*height);
				memcpy(copy,pixels,width * height);
				*compatablePixels = copy;
				if ( newPitch )
					*newPitch = width;
			}
			break;
		case D3DFMT_A4R4G4B4:
			{
				int textureElementSize = 2;
				const unsigned char* glpixels = (const unsigned char*) pixels;
				char* dxpixels = StickyAlloc(textureElementSize * width * height);
				switch ( internalformat )
				{
				default:
					LocalDebugBreak();
					break;
				case 1:
					{
						for(int y = 0; y < height; y++)
						{
							for(int x = 0; x < width; x++)
							{
								unsigned short* dp = (unsigned short*) (dxpixels + (y*width+x)*textureElementSize);
								const unsigned char* sp = glpixels + (y*width+x);
								unsigned short v;
								unsigned short s = 0xf & (sp[0] >> 4);
								v = s; // blue
								v |= s << 4; // green
								v |= s << 8; // red
								v |= s << 12; // alpha
								*dp = v;
							}
						}
					}
					break;
				case 3:
					{
						for(int y = 0; y < height; y++)
						{
							for(int x = 0; x < width; x++)
							{
								unsigned short* dp = (unsigned short*) (dxpixels + (y*width+x)*textureElementSize);
								const unsigned char* sp = glpixels + (y*width+x)*4;
								unsigned short v;
								v = (0xf & (sp[2] >> 4)); // blue
								v |= (0xf & (sp[1] >> 4)) << 4; // green
								v |= (0xf & (sp[0] >> 4)) << 8; // red
								v |= 0xf000; // alpha
								*dp = v;
							}
						}
					}
					break;
				case 4:
					{
						for(int y = 0; y < height; y++)
						{
							for(int x = 0; x < width; x++)
							{
								unsigned short* dp = (unsigned short*)(dxpixels + (y*width+x)*textureElementSize);
								const unsigned char* sp = glpixels + (y*width+x)*4;
								unsigned short v;
								v = (0xf & (sp[2] >> 4)); // blue
								v |= (0xf & (sp[1] >> 4)) << 4; // green
								v |= (0xf & (sp[0] >> 4)) << 8; // red
								v |= (0xf & (sp[3] >> 4)) << 12; // alpha
								*dp = v;
							}
						}
					}
					break;
				}
				*compatablePixels = dxpixels;
				if ( newPitch )
					*newPitch = 2 * width;
			}
			break;
		case D3DFMT_R5G6B5:
			{
				int textureElementSize = 2;
				const char* glpixels = (const char*) pixels;
				char* dxpixels = StickyAlloc(textureElementSize * width * height);
				switch ( internalformat ) 
				{
				default:
					LocalDebugBreak();
					break;
				case 1:
					{
						for(int y = 0; y < height; y++){
							for(int x = 0; x < width; x++){
								unsigned short* dp = (unsigned short*) (dxpixels + (y*width+x)*textureElementSize);
								const char* sp = glpixels + (y*width+x);
								unsigned short v;
								v = (0x1f & (sp[0] >> 3)); // blue
								v |= (0x3f & (sp[0] >> 2)) << 5; // green
								v |= (0x1f & (sp[0] >> 3)) << 11; // red
								*dp = v;
							}
						}
					}
					break;
				case 3:
					{
						for(int y = 0; y < height; y++)
						{
							for(int x = 0; x < width; x++)
							{
								unsigned short* dp = (unsigned short*) (dxpixels + (y*width+x)*textureElementSize);
								const char* sp = glpixels + (y*width+x)*4;
								unsigned short v;
								v = (0x1f & (sp[2] >> 3)); // blue
								v |= (0x3f & (sp[1] >> 2)) << 5; // green
								v |= (0x1f & (sp[0] >> 3)) << 11; // red
								*dp = v;
							}
						}
					}
					break;
				case 4:
					{
						for(int y = 0; y < height; y++)
						{
							for(int x = 0; x < width; x++)
							{
								unsigned short* dp = (unsigned short*) (dxpixels + (y*width+x)*textureElementSize);
								const char* sp = glpixels + (y*width+x)*4;
								unsigned short v;
								v = (0x1f & (sp[2] >> 3)); // blue
								v |= (0x3f & (sp[1] >> 2)) << 5; // green
								v |= (0x1f & (sp[0] >> 3)) << 11; // red
								*dp = v;
							}
						}
					}
					break;
				}
				*compatablePixels = dxpixels;
				if ( newPitch )
					*newPitch = 2 * width;
			}
			break;
		case D3DFMT_X1R5G5B5:
			{
				int textureElementSize = 2;
				const char* glpixels = (const char*) pixels;
				char* dxpixels = StickyAlloc(textureElementSize * width * height);
				switch ( internalformat ) 
				{
				default:
					LocalDebugBreak();
					break;
				case 1:
					{
#define RGBTOR5G5B5(R, G, B) (0x8000 |  (0x1f & ((B) >> 3)) | ((0x1f & ((G) >> 3)) << 5) | ((0x1f & ((R) >> 3)) << 10))
#define Y5TOR5G5B5(Y) (0x8000 | ((Y) << 10) | ((Y) << 5) | (Y))
						static const unsigned short table[32] = {
							Y5TOR5G5B5(0), Y5TOR5G5B5(1), Y5TOR5G5B5(2), Y5TOR5G5B5(3),
							Y5TOR5G5B5(4), Y5TOR5G5B5(5), Y5TOR5G5B5(6), Y5TOR5G5B5(7),
							Y5TOR5G5B5(8), Y5TOR5G5B5(9), Y5TOR5G5B5(10), Y5TOR5G5B5(11),
							Y5TOR5G5B5(12), Y5TOR5G5B5(13), Y5TOR5G5B5(14), Y5TOR5G5B5(15),
							Y5TOR5G5B5(16), Y5TOR5G5B5(17), Y5TOR5G5B5(18), Y5TOR5G5B5(19),
							Y5TOR5G5B5(20), Y5TOR5G5B5(21), Y5TOR5G5B5(22), Y5TOR5G5B5(23),
							Y5TOR5G5B5(24), Y5TOR5G5B5(25), Y5TOR5G5B5(26), Y5TOR5G5B5(27),
							Y5TOR5G5B5(28), Y5TOR5G5B5(29), Y5TOR5G5B5(30), Y5TOR5G5B5(31)
						};
						unsigned short* dp = (unsigned short*) dxpixels;
						const unsigned char* sp = (const unsigned char*) glpixels;
						int numPixels = height * width;
						int i = numPixels >> 2;
						while(i > 0) {
							*dp++ = table[(*sp++) >> 3];
							*dp++ = table[(*sp++) >> 3];
							*dp++ = table[(*sp++) >> 3];
							*dp++ = table[(*sp++) >> 3];
							--i;
						}

						i = numPixels & 3;
						while(i > 0) {
							*dp++ = table[(*sp++) >> 3];
							--i;
						}
					}
					break;
				case 3:
					{
						for(int y = 0; y < height; y++)
						{
							for(int x = 0; x < width; x++)
							{
								unsigned short* dp = (unsigned short*) (dxpixels + (y*width+x)*textureElementSize);
								const unsigned char* sp = (const unsigned char*) glpixels + (y*width+x)*4;
								unsigned short v;
								v = (sp[2] >> 3); // blue
								v |= (sp[1] >> 3) << 5; // green
								v |= (sp[0] >> 3) << 10; // red
								v |= 0x8000; // alpha
								*dp = v;
							}
						}
					}
					break;
				case 4:
					{
						for(int y = 0; y < height; y++)
						{
							for(int x = 0; x < width; x++)
							{
								unsigned short* dp = (unsigned short*) (dxpixels + (y*width+x)*textureElementSize);
								const unsigned char* sp = (const unsigned char*) glpixels + (y*width+x)*4;
								unsigned short v;
								v = (sp[2] >> 3); // blue
								v |= (sp[1] >> 3) << 5; // green
								v |= (sp[0] >> 3) << 10; // red
								v |= 0x8000; // alpha
								*dp = v;
							}
						}
					}
					break;
				}
				*compatablePixels = dxpixels;
				if ( newPitch ) {
					*newPitch = 2 * width;
				}
			}
			break;
		case D3DFMT_A1R5G5B5:
			{
				int textureElementSize = 2;
				const char* glpixels = (const char*) pixels;
				char* dxpixels = StickyAlloc(textureElementSize * width * height);
				switch ( internalformat ) 
				{
				default:
					LocalDebugBreak();
					break;
				case 1:
					{
						for(int y = 0; y < height; y++){
							for(int x = 0; x < width; x++){
								unsigned short* dp = (unsigned short*) (dxpixels + (y*width+x)*textureElementSize);
								const char* sp = glpixels + (y*width+x);
								unsigned short v;
								v = (0x1f & (sp[0] >> 3)); // blue
								v |= (0x1f & (sp[0] >> 3)) << 5; // green
								v |= (0x1f & (sp[0] >> 3)) << 10; // red
								v |= (0x01 & (sp[0] >> 7)) << 15; // alpha
								*dp = v;
							}
						}
					}
					break;
				case 3:
					{
						for(int y = 0; y < height; y++){
							for(int x = 0; x < width; x++){
								unsigned short* dp = (unsigned short*) (dxpixels + (y*width+x)*textureElementSize);
								const char* sp = glpixels + (y*width+x)*4;
								unsigned short v;
								v = (0x1f & (sp[2] >> 3)); // blue
								v |= (0x1f & (sp[1] >> 3)) << 5; // green
								v |= (0x1f & (sp[0] >> 3)) << 10; // red
								v |= 0x8000; // alpha
								*dp = v;
							}
						}
					}
					break;
				case 4:
					{
						for(int y = 0; y < height; y++)
						{
							for(int x = 0; x < width; x++)
							{
								unsigned short* dp = (unsigned short*) (dxpixels + (y*width+x)*textureElementSize);
								const char* sp = glpixels + (y*width+x)*4;
								unsigned short v;
								v = (0x1f & (sp[2] >> 3)); // blue
								v |= (0x1f & (sp[1] >> 3)) << 5; // green
								v |= (0x1f & (sp[0] >> 3)) << 10; // red
								v |= (0x01 & (sp[3] >> 7)) << 15; // alpha
								*dp = v;
							}
						}
					}
					break;
				}
				*compatablePixels = dxpixels;
				if ( newPitch ) {
					*newPitch = 2 * width;
				}
			}
			break;
		case D3DFMT_X8R8G8B8:
		case D3DFMT_A8R8G8B8:
			{
				int textureElementSize = 4;
				const char* glpixels = (const char*) pixels;
				char* dxpixels = StickyAlloc(textureElementSize * width * height);
				switch ( internalformat )
				{
				default:
					LocalDebugBreak();
					break;
				case 1:
					{
						for(int y = 0; y < height; y++){
							for(int x = 0; x < width; x++){
								char* dp = dxpixels + (y*width+x)*textureElementSize;
								const char* sp = glpixels + (y*width+x);
								dp[0] = sp[0]; // blue
								dp[1] = sp[0]; // green
								dp[2] = sp[0]; // red
								dp[3] = sp[0];
							}
						}
					}
					break;
				case 3:
					{
						for(int y = 0; y < height; y++){
							for(int x = 0; x < width; x++){
								unsigned char* dp = (unsigned char*) dxpixels + (y*width+x)*textureElementSize;
								const unsigned char* sp = (unsigned char*) glpixels + (y*width+x)*4;
								dp[0] = sp[2]; // blue
								dp[1] = sp[1]; // green
								dp[2] = sp[0]; // red
								dp[3] = 0xff;
							}
						}
					}
					break;
				case 4:
					{
						for(int y = 0; y < height; y++)
						{
							for(int x = 0; x < width; x++)
							{
								char* dp = dxpixels + (y*width+x)*textureElementSize;
								const char* sp = glpixels + (y*width+x)*4;
								dp[0] = sp[2]; // blue
								dp[1] = sp[1]; // green
								dp[2] = sp[0]; // red
								dp[3] = sp[3]; // alpha
							}
						}
					}
					break;
				}
				*compatablePixels = dxpixels;
				if ( newPitch ) 
					*newPitch = 4 * width;
			}
		}
		return hr;
	}
#pragma warning( pop )
};

void /*APIENTRY*/ glAlphaFunc (GLenum func, GLclampf ref)
{
	gFakeGL->glAlphaFunc(func, ref);
}

void /*APIENTRY*/ glBegin (GLenum mode)
{
	gFakeGL->glBegin(mode);
}

void /*APIENTRY*/ glBlendFunc (GLenum sfactor, GLenum dfactor)
{
	gFakeGL->glBlendFunc(sfactor, dfactor);
}

void /*APIENTRY*/ glClear (GLbitfield mask)
{
	gFakeGL->glClear(mask);
}

void /*APIENTRY*/ glClearColor (GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
	gFakeGL->glClearColor(red, green, blue, alpha);
}

void /*APIENTRY*/ glColor3f (GLfloat red, GLfloat green, GLfloat blue)
{
	gFakeGL->glColor3f(red, green, blue);
}

void /*APIENTRY*/ glColor3ubv (const GLubyte *v)
{
	gFakeGL->glColor3ubv(v);
}

void /*APIENTRY*/ glColor4ubv (const GLubyte *v)
{
	gFakeGL->glColor4ubv(v);
}

void /*APIENTRY*/ glColor4f (GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
	gFakeGL->glColor4f(red, green, blue, alpha);
}

void /*APIENTRY*/ glColor4ub (GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha)
{
	gFakeGL->glColor4ub(red, green, blue, alpha);
}

void /*APIENTRY*/ glColor4fv (const GLfloat *v)
{
	gFakeGL->glColor4fv(v);
}

extern "C" void JkaFakeglClipPlane0( const GLdouble *equation )
{
	if (gFakeGL)
	{
		gFakeGL->glClipPlane0(equation);
	}
}

extern "C" void JkaFakeglScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
	if (gFakeGL)
	{
		gFakeGL->glScissor(x, y, width, height);
	}
}

void /*APIENTRY*/ glCullFace (GLenum mode)
{
	gFakeGL->glCullFace(mode);
}

void /*APIENTRY*/ glDepthFunc (GLenum func)
{
	gFakeGL->glDepthFunc(func);
}

void /*APIENTRY*/ glDepthMask (GLboolean flag)
{
	gFakeGL->glDepthMask(flag);
}

void /*APIENTRY*/ glDepthRange (GLclampd zNear, GLclampd zFar)
{
	gFakeGL->glDepthRange(zNear, zFar);
}

void /*APIENTRY*/ glDisable (GLenum cap)
{
	gFakeGL->glDisable(cap);
}

void /*APIENTRY*/ glDrawBuffer (GLenum mode)
{
	gFakeGL->glDrawBuffer(mode);
}

void /*APIENTRY*/ glEnable (GLenum cap)
{
	gFakeGL->glEnable(cap);
}

void /*APIENTRY*/ glEnd (void)
{
	return; // Does nothing
//	gFakeGL->glEnd();
}

void /*APIENTRY*/ glFinish (void)
{
	gFakeGL->glFinish();
}

extern "C" void JkaFakeglFogf(GLenum pname, GLfloat param)
{
	if (gFakeGL)
	{
		gFakeGL->glFogf(pname, param);
	}
}

extern "C" void JkaFakeglFogfv(GLenum pname, const GLfloat *params)
{
	if (gFakeGL)
	{
		gFakeGL->glFogfv(pname, params);
	}
}

extern "C" void JkaFakeglFogi(GLenum pname, GLint param)
{
	if (gFakeGL)
	{
		gFakeGL->glFogi(pname, param);
	}
}

void /*APIENTRY*/ glFrustum (GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar)
{
	gFakeGL->glFrustum(left, right, bottom, top, zNear, zFar);
}

void /*APIENTRY*/ glGetFloatv (GLenum pname, GLfloat *params)
{
	gFakeGL->glGetFloatv(pname, params);
}

const GLubyte* /*APIENTRY*/ glGetString (GLenum name)
{
	return gFakeGL->glGetString(name);
}

void /*APIENTRY*/ glHint (GLenum target, GLenum mode)
{
	gFakeGL->glHint(target, mode);
}

GLboolean /*APIENTRY*/ glIsEnabled(GLenum cap)
{
	return gFakeGL->glIsEnabled(cap);
}

void /*APIENTRY*/ glLoadIdentity (void)
{
	gFakeGL->glLoadIdentity();
}

void /*APIENTRY*/ glLoadMatrixf (const GLfloat *m)
{
	gFakeGL->glLoadMatrixf(m);
}

void /*APIENTRY*/ glMatrixMode (GLenum mode)
{
	gFakeGL->glMatrixMode(mode);
}

void /*APIENTRY*/  glOrtho (GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar)
{
	gFakeGL->glOrtho(left, right, top, bottom, zNear, zFar);
}

void /*APIENTRY*/ glPolygonMode (GLenum face, GLenum mode)
{
	gFakeGL->glPolygonMode(face, mode);
}

void /*APIENTRY*/ glPopMatrix (void)
{
	gFakeGL->glPopMatrix();
}

void /*APIENTRY*/ glPushMatrix (void)
{
	gFakeGL->glPushMatrix();
}

void /*APIENTRY*/ glReadBuffer (GLenum mode)
{
	gFakeGL->glReadBuffer(mode);
}

void /*APIENTRY*/glReadPixels (GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels)
{
	gFakeGL->glReadPixels(x, y, width, height, format, type, pixels);
}

void /*APIENTRY*/ glRotatef (GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
	gFakeGL->glRotatef(angle, x, y, z);
}

void /*APIENTRY*/ glScalef (GLfloat x, GLfloat y, GLfloat z)
{
	gFakeGL->glScalef(x, y, z);
}

void /*APIENTRY*/ glShadeModel (GLenum mode)
{
	gFakeGL->glShadeModel(mode);
}

void /*APIENTRY*/ glTexCoord2f (GLfloat s, GLfloat t)
{
	gFakeGL->glTexCoord2f(s, t);
}

void /*APIENTRY*/ glTexEnvf (GLenum target, GLenum pname, GLfloat param)
{
	gFakeGL->glTexEnvf(target, pname, param);
}

void /*APIENTRY*/ glTexImage2D (GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
	gFakeGL->glTexImage2D(target, level, internalformat, width, height, border, format, type, pixels);
}

void /*APIENTRY*/ glTexParameterf (GLenum target, GLenum pname, GLfloat param)
{
	gFakeGL->glTexParameterf(target, pname, param);
}

void /*APIENTRY*/ glTexSubImage2D (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels)
{
	gFakeGL->glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels);
}

void /*APIENTRY*/ glTranslatef (GLfloat x, GLfloat y, GLfloat z)
{
	gFakeGL->glTranslatef(x, y, z);
}

void /*APIENTRY*/ glVertex2f (GLfloat x, GLfloat y)
{
	gFakeGL->glVertex2f(x, y);
}

void /*APIENTRY*/ glVertex3f (GLfloat x, GLfloat y, GLfloat z)
{
	gFakeGL->glVertex3f(x, y, z);
}

void /*APIENTRY*/ glVertex3fv (const GLfloat *v)
{
	gFakeGL->glVertex3fv(v);
}

void /*APIENTRY*/ glViewport (GLint x, GLint y, GLsizei width, GLsizei height)
{
	gFakeGL->glViewport(x, y, width, height);
}

extern "C" int JkaFakeglUploadDDS(GLint internalformat, GLsizei width, GLsizei height,
	GLint mipcount, const GLvoid *pixels, DWORD pixelBytes)
{
	if (!gFakeGL)
	{
		return 0;
	}
	return gFakeGL->UploadDDSTexture(internalformat, width, height,
		mipcount, pixels, pixelBytes) ? 1 : 0;
}

extern "C" void JkaFakeglDeleteTexture(GLuint texture)
{
	if (!gFakeGL)
		return;
	gFakeGL->DeleteTexture(texture);
}

extern "C" int JkaFakeglIsTexture(GLuint texture)
{
	if (!gFakeGL)
		return 0;
	return gFakeGL->IsTexture(texture) ? 1 : 0;
}

//HDC gHDC;
//HGLRC gHGLRC;

HGLRC /*APIENTRY*/ wglCreateContext(/*maindc*/)
{
	/*return (HGLRC)*/gFakeGL = new FakeGL(/*mainwindow*/);

	// We don't return a handle on XBox
	if(!gFakeGL)
		return (HGLRC)0; 

	return (HGLRC)1;
}

BOOL /*WINAPI*/ wglDeleteContext(/*HGLRC hglrc*/)
{
	FakeGL* pFakeGL = gFakeGL;//(FakeGL*) hglrc;
	delete pFakeGL;
	
    pFakeGL = NULL;
	return true;
}

/*
HGLRC WINAPI wglGetCurrentContext(VOID)
{
	return gHGLRC;
}

HDC   WINAPI wglGetCurrentDC(VOID)
{ 
	return gHDC;
}
*/

void /*APIENTRY*/ glBindTexture(GLenum target, GLuint texture)
{
	gFakeGL->glBindTexture(target, texture);
}

static void /*APIENTRY*/ BindTextureExt(GLenum target, GLuint texture)
{
	gFakeGL->glBindTexture(target, texture);
}

static void /*APIENTRY*/ MTexCoord2fSGIS(GLenum target, GLfloat s, GLfloat t)
{
	gFakeGL->glMTexCoord2fSGIS(target, s, t);
}

static void /*APIENTRY*/ SelectTextureSGIS(GLenum target)
{
	gFakeGL->glSelectTextureSGIS(target);
}

extern "C" void JkaFakeglMTexCoord2fSGIS(GLenum target, GLfloat s, GLfloat t)
{
	gFakeGL->glMTexCoord2fSGIS(target, s, t);
}

extern "C" void JkaFakeglSelectTextureSGIS(GLenum target)
{
	gFakeGL->glSelectTextureSGIS(target);
}

extern "C" GLboolean JkaFakeglIsEnabled(GLenum cap)
{
	return gFakeGL->glIsEnabled(cap);
}

extern "C" void JkaFakeglEnable(GLenum cap)
{
	gFakeGL->glEnable(cap);
}

extern "C" void JkaFakeglDisable(GLenum cap)
{
	gFakeGL->glDisable(cap);
}

// Type cast unsafe conversion from 
#pragma warning( push )
#pragma warning( disable : 4191)

PROC /*APIENTRY*/ wglGetProcAddress(LPCSTR s)
{
	static LPCSTR kBindTextureEXT = "glBindTextureEXT";
	static LPCSTR kMTexCoord2fSGIS = "glMTexCoord2fSGIS"; // Multitexture
	static LPCSTR kSelectTextureSGIS = "glSelectTextureSGIS";
	if ( strncmp(s, kBindTextureEXT, sizeof(kBindTextureEXT)-1) == 0)
	{
		return (PROC) BindTextureExt;
	}
	else if ( strncmp(s, kMTexCoord2fSGIS, sizeof(kMTexCoord2fSGIS)-1) == 0)
	{
		return (PROC) MTexCoord2fSGIS;
	}
	else if ( strncmp(s, kSelectTextureSGIS, sizeof(kSelectTextureSGIS)-1) == 0)
	{
		return (PROC) SelectTextureSGIS;
	}
	// LocalDebugBreak();
	return 0;
}

#pragma warning( pop )

BOOL /*WINAPI*/ wglMakeCurrent(/*HDC hdc, HGLRC hglrc*/)
{
	// Pointer assigned in CreateContext
	/* 
	gHDC = hdc;
	gHGLRC = hglrc;
	gFakeGL = (FakeGL*) hglrc;
	*/
	if(!gFakeGL)
		return FALSE;

	return TRUE;
}

void d3dEvictTextures()
{
	gFakeGL->EvictTextures();
}

int d3dIsResolutionHD()
{	
	// Check if we have component cables
	if((XGetAVPack() == XC_AV_PACK_HDTV) && (XGetVideoFlags() & XC_VIDEO_FLAGS_HDTV_720p))
		return TRUE;

	return FALSE;
}

void d3dSetMode(int width, int height, int bpp, int zbpp, int vmode)
{
	gWidth = width;
	gHeight = height;
	gVideoMode = vmode;
}

void /*WINAPI*/ FakeSwapBuffers()
{
	if (!gFakeGL)
		return;

	gFakeGL->SwapBuffers();
}

void d3dSetGammaRamp(const unsigned char* gammaTable)
{
	gFakeGL->SetGammaRamp(gammaTable);
}

void d3dInitSetForce16BitTextures(int force16bitTextures)
{
	// Called before gFakeGL exits. That's why we set a global
	g_force16bitTextures = force16bitTextures != 0; 
}

void d3dHint_GenerateMipMaps(int value)
{
	gFakeGL->Hint_GenerateMipMaps(value);
}

float d3dGetD3DDriverVersion()
{
	return 0.81f;
}
