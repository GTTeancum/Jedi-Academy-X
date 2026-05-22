#include "../server/exe_headers.h"

#include "d3dx8.h"
#include "xb_log.h"

#include <new>

namespace
{
    static void LogCompatOnce(const char* msg, bool& flag)
    {
        if (!flag) {
            XBLog_Write(msg);
            flag = true;
        }
    }

    class MatrixStackImpl : public ID3DXMatrixStack
    {
    public:
        MatrixStackImpl()
            : m_refCount(1),
              m_top(0)
        {
            XGMatrixIdentity(&m_stack[0]);
        }

        HRESULT WINAPI QueryInterface(REFIID riid, void **ppvObject)
        {
            if (!ppvObject) {
                return E_POINTER;
            }

            *ppvObject = NULL;
            if (riid == IID_IUnknown || riid == IID_ID3DXMatrixStack) {
                *ppvObject = static_cast<ID3DXMatrixStack*>(this);
                AddRef();
                return S_OK;
            }

            return E_NOINTERFACE;
        }

        ULONG WINAPI AddRef()
        {
            return ++m_refCount;
        }

        ULONG WINAPI Release()
        {
            ULONG refCount = --m_refCount;
            if (refCount == 0) {
                delete this;
            }
            return refCount;
        }

        HRESULT WINAPI Pop()
        {
            if (m_top == 0) {
                return D3DERR_INVALIDCALL;
            }

            --m_top;
            return S_OK;
        }

        HRESULT WINAPI Push()
        {
            if (m_top >= kMaxDepth - 1) {
                return E_FAIL;
            }

            m_stack[m_top + 1] = m_stack[m_top];
            ++m_top;
            return S_OK;
        }

        HRESULT WINAPI LoadIdentity()
        {
            XGMatrixIdentity(&m_stack[m_top]);
            return S_OK;
        }

        HRESULT WINAPI LoadMatrix(CONST D3DXMATRIX* pM)
        {
            if (!pM) {
                return E_POINTER;
            }

            m_stack[m_top] = *reinterpret_cast<const XGMATRIX*>(pM);
            return S_OK;
        }

        HRESULT WINAPI MultMatrix(CONST D3DXMATRIX* pM)
        {
            if (!pM) {
                return E_POINTER;
            }

            XGMATRIX result;
            XGMatrixMultiply(&result, &m_stack[m_top], reinterpret_cast<const XGMATRIX*>(pM));
            m_stack[m_top] = result;
            return S_OK;
        }

        HRESULT WINAPI MultMatrixLocal(CONST D3DXMATRIX* pM)
        {
            if (!pM) {
                return E_POINTER;
            }

            XGMATRIX result;
            XGMatrixMultiply(&result, reinterpret_cast<const XGMATRIX*>(pM), &m_stack[m_top]);
            m_stack[m_top] = result;
            return S_OK;
        }

        HRESULT WINAPI RotateAxis(CONST D3DXVECTOR3* pV, FLOAT Angle)
        {
            if (!pV) {
                return E_POINTER;
            }

            XGMATRIX rotation;
            XGMatrixRotationAxis(&rotation, reinterpret_cast<const XGVECTOR3*>(pV), Angle);
            return MultMatrix(reinterpret_cast<const D3DXMATRIX*>(&rotation));
        }

        HRESULT WINAPI RotateAxisLocal(CONST D3DXVECTOR3* pV, FLOAT Angle)
        {
            if (!pV) {
                return E_POINTER;
            }

            XGMATRIX rotation;
            XGMatrixRotationAxis(&rotation, reinterpret_cast<const XGVECTOR3*>(pV), Angle);
            return MultMatrixLocal(reinterpret_cast<const D3DXMATRIX*>(&rotation));
        }

        HRESULT WINAPI Scale(FLOAT x, FLOAT y, FLOAT z)
        {
            XGMATRIX scale;
            XGMatrixScaling(&scale, x, y, z);
            return MultMatrix(reinterpret_cast<const D3DXMATRIX*>(&scale));
        }

        HRESULT WINAPI ScaleLocal(FLOAT x, FLOAT y, FLOAT z)
        {
            XGMATRIX scale;
            XGMatrixScaling(&scale, x, y, z);
            return MultMatrixLocal(reinterpret_cast<const D3DXMATRIX*>(&scale));
        }

        HRESULT WINAPI Translate(FLOAT x, FLOAT y, FLOAT z)
        {
            XGMATRIX translation;
            XGMatrixTranslation(&translation, x, y, z);
            return MultMatrix(reinterpret_cast<const D3DXMATRIX*>(&translation));
        }

        HRESULT WINAPI TranslateLocal(FLOAT x, FLOAT y, FLOAT z)
        {
            XGMATRIX translation;
            XGMatrixTranslation(&translation, x, y, z);
            return MultMatrixLocal(reinterpret_cast<const D3DXMATRIX*>(&translation));
        }

        D3DXMATRIX* WINAPI GetTop()
        {
            return reinterpret_cast<D3DXMATRIX*>(&m_stack[m_top]);
        }

    private:
        enum { kMaxDepth = 32 };

        ~MatrixStackImpl()
        {
        }

        ULONG m_refCount;
        int m_top;
        XGMATRIX m_stack[kMaxDepth];
    };

    static UINT BytesPerPixel(D3DFORMAT format)
    {
        switch (format) {
        case D3DFMT_A8R8G8B8:
        case D3DFMT_X8R8G8B8:
        case D3DFMT_LIN_A8R8G8B8:
        case D3DFMT_LIN_X8R8G8B8:
            return 4;
        case D3DFMT_R5G6B5:
        case D3DFMT_A4R4G4B4:
        case D3DFMT_LIN_R5G6B5:
        case D3DFMT_LIN_A4R4G4B4:
            return 2;
        case D3DFMT_A8:
        case D3DFMT_LIN_A8:
        case D3DFMT_L8:
        case D3DFMT_LIN_L8:
            return 1;
        default:
            return 0;
        }
    }

    static D3DCOLOR ReadColor(const BYTE* src, D3DFORMAT format, UINT sourceRowBytes)
    {
        switch (sourceRowBytes) {
        case 3:
            return D3DCOLOR_XRGB(src[0], src[1], src[2]);
        case 4:
            return (format == D3DFMT_A8R8G8B8 || format == D3DFMT_LIN_A8R8G8B8)
                ? D3DCOLOR_ARGB(src[3], src[0], src[1], src[2])
                : D3DCOLOR_XRGB(src[0], src[1], src[2]);
        case 2:
            {
                WORD packed = *reinterpret_cast<const WORD*>(src);
                BYTE r = (BYTE)(((packed >> 11) & 0x1f) * 255 / 31);
                BYTE g = (BYTE)(((packed >> 5) & 0x3f) * 255 / 63);
                BYTE b = (BYTE)((packed & 0x1f) * 255 / 31);
                return D3DCOLOR_XRGB(r, g, b);
            }
        case 1:
            return D3DCOLOR_XRGB(src[0], src[0], src[0]);
        default:
            switch (format) {
            case D3DFMT_A8R8G8B8:
            case D3DFMT_LIN_A8R8G8B8:
                return D3DCOLOR_ARGB(src[3], src[0], src[1], src[2]);
            case D3DFMT_X8R8G8B8:
            case D3DFMT_LIN_X8R8G8B8:
                return D3DCOLOR_XRGB(src[0], src[1], src[2]);
            default:
                return 0;
            }
        }
    }

    static void WriteColor(BYTE* dest, D3DFORMAT format, D3DCOLOR color)
    {
        BYTE a = (BYTE)((color >> 24) & 0xff);
        BYTE r = (BYTE)((color >> 16) & 0xff);
        BYTE g = (BYTE)((color >> 8) & 0xff);
        BYTE b = (BYTE)(color & 0xff);

        switch (format) {
        case D3DFMT_A8R8G8B8:
        case D3DFMT_LIN_A8R8G8B8:
            dest[0] = r;
            dest[1] = g;
            dest[2] = b;
            dest[3] = a;
            break;
        case D3DFMT_X8R8G8B8:
        case D3DFMT_LIN_X8R8G8B8:
            dest[0] = r;
            dest[1] = g;
            dest[2] = b;
            dest[3] = 0xff;
            break;
        case D3DFMT_R5G6B5:
        case D3DFMT_LIN_R5G6B5:
            *reinterpret_cast<WORD*>(dest) =
                (WORD)(((r * 31 / 255) << 11) |
                       ((g * 63 / 255) << 5) |
                       (b * 31 / 255));
            break;
        case D3DFMT_A4R4G4B4:
        case D3DFMT_LIN_A4R4G4B4:
            *reinterpret_cast<WORD*>(dest) =
                (WORD)(((a * 15 / 255) << 12) |
                       ((r * 15 / 255) << 8) |
                       ((g * 15 / 255) << 4) |
                       (b * 15 / 255));
            break;
        case D3DFMT_A8:
        case D3DFMT_LIN_A8:
            dest[0] = a;
            break;
        case D3DFMT_L8:
        case D3DFMT_LIN_L8:
            dest[0] = (BYTE)((r + g + b) / 3);
            break;
        default:
            break;
        }
    }

    static RECT MakeFullRect(UINT width, UINT height)
    {
        RECT rect;
        rect.left = 0;
        rect.top = 0;
        rect.right = width;
        rect.bottom = height;
        return rect;
    }
}

extern "C"
{
HRESULT WINAPI D3DXCreateMatrixStack(DWORD Flags, LPD3DXMATRIXSTACK *ppStack)
{
    (void)Flags;

    if (!ppStack) {
        return E_POINTER;
    }

    *ppStack = new (std::nothrow) MatrixStackImpl();
    return *ppStack ? S_OK : E_OUTOFMEMORY;
}

HRESULT WINAPI D3DXCreateTextureFromFileExA(
    LPDIRECT3DDEVICE8 pDevice,
    LPCSTR pSrcFile,
    UINT Width,
    UINT Height,
    UINT MipLevels,
    DWORD Usage,
    D3DFORMAT Format,
    D3DPOOL Pool,
    DWORD Filter,
    DWORD MipFilter,
    D3DCOLOR ColorKey,
    void *pSrcInfo,
    PALETTEENTRY *pPalette,
    LPDIRECT3DTEXTURE8 *ppTexture)
{
    static bool s_logged = false;
    LogCompatOnce("JA: d3dx8_compat - D3DXCreateTextureFromFileExA not implemented", s_logged);
    (void)pDevice; (void)pSrcFile; (void)Width; (void)Height; (void)MipLevels;
    (void)Usage; (void)Format; (void)Pool; (void)Filter; (void)MipFilter;
    (void)ColorKey; (void)pSrcInfo; (void)pPalette;
    if (ppTexture) {
        *ppTexture = NULL;
    }
    return E_NOTIMPL;
}

HRESULT WINAPI D3DXCreateTextureFromFileExW(
    LPDIRECT3DDEVICE8 pDevice,
    LPCWSTR pSrcFile,
    UINT Width,
    UINT Height,
    UINT MipLevels,
    DWORD Usage,
    D3DFORMAT Format,
    D3DPOOL Pool,
    DWORD Filter,
    DWORD MipFilter,
    D3DCOLOR ColorKey,
    void *pSrcInfo,
    PALETTEENTRY *pPalette,
    LPDIRECT3DTEXTURE8 *ppTexture)
{
    static bool s_logged = false;
    LogCompatOnce("JA: d3dx8_compat - D3DXCreateTextureFromFileExW not implemented", s_logged);
    (void)pDevice; (void)pSrcFile; (void)Width; (void)Height; (void)MipLevels;
    (void)Usage; (void)Format; (void)Pool; (void)Filter; (void)MipFilter;
    (void)ColorKey; (void)pSrcInfo; (void)pPalette;
    if (ppTexture) {
        *ppTexture = NULL;
    }
    return E_NOTIMPL;
}

HRESULT WINAPI D3DXLoadSurfaceFromSurface(
    LPDIRECT3DSURFACE8 pDestSurface,
    CONST PALETTEENTRY* pDestPalette,
    CONST RECT* pDestRect,
    LPDIRECT3DSURFACE8 pSrcSurface,
    CONST PALETTEENTRY* pSrcPalette,
    CONST RECT* pSrcRect,
    DWORD Filter,
    D3DCOLOR ColorKey)
{
    (void)pDestPalette;
    (void)pSrcPalette;
    (void)Filter;
    (void)ColorKey;

    if (!pDestSurface || !pSrcSurface) {
        XBLog_Write("JA: d3dx8_compat - D3DXLoadSurfaceFromSurface got null surface");
        return E_POINTER;
    }

    D3DSURFACE_DESC destDesc;
    D3DSURFACE_DESC srcDesc;
    if (FAILED(pDestSurface->GetDesc(&destDesc)) || FAILED(pSrcSurface->GetDesc(&srcDesc))) {
        XBLog_Write("JA: d3dx8_compat - D3DXLoadSurfaceFromSurface GetDesc failed");
        return E_FAIL;
    }

    RECT destRect = pDestRect ? *pDestRect : MakeFullRect(destDesc.Width, destDesc.Height);
    RECT srcRect = pSrcRect ? *pSrcRect : MakeFullRect(srcDesc.Width, srcDesc.Height);

    D3DLOCKED_RECT destLock;
    D3DLOCKED_RECT srcLock;
    if (FAILED(pDestSurface->LockRect(&destLock, &destRect, 0))) {
        XBLog_Write("JA: d3dx8_compat - D3DXLoadSurfaceFromSurface dest LockRect failed");
        return E_FAIL;
    }
    if (FAILED(pSrcSurface->LockRect(&srcLock, &srcRect, 0))) {
        XBLog_Write("JA: d3dx8_compat - D3DXLoadSurfaceFromSurface src LockRect failed");
        pDestSurface->UnlockRect();
        return E_FAIL;
    }

    UINT destBpp = BytesPerPixel(destDesc.Format);
    UINT srcBpp = BytesPerPixel(srcDesc.Format);
    if (!destBpp || !srcBpp) {
        pSrcSurface->UnlockRect();
        pDestSurface->UnlockRect();
        return S_OK;
    }

    UINT destWidth = destRect.right - destRect.left;
    UINT destHeight = destRect.bottom - destRect.top;
    UINT srcWidth = srcRect.right - srcRect.left;
    UINT srcHeight = srcRect.bottom - srcRect.top;

    for (UINT y = 0; y < destHeight; ++y) {
        UINT srcY = srcHeight ? (y * srcHeight) / destHeight : 0;
        BYTE* destRow = static_cast<BYTE*>(destLock.pBits) + y * destLock.Pitch;
        const BYTE* srcRow = static_cast<const BYTE*>(srcLock.pBits) + srcY * srcLock.Pitch;
        for (UINT x = 0; x < destWidth; ++x) {
            UINT srcX = srcWidth ? (x * srcWidth) / destWidth : 0;
            const BYTE* srcPixel = srcRow + srcX * srcBpp;
            BYTE* destPixel = destRow + x * destBpp;
            WriteColor(destPixel, destDesc.Format, ReadColor(srcPixel, srcDesc.Format, srcBpp));
        }
    }

    pSrcSurface->UnlockRect();
    pDestSurface->UnlockRect();
    return S_OK;
}

HRESULT WINAPI D3DXLoadSurfaceFromMemory(
    LPDIRECT3DSURFACE8 pDestSurface,
    CONST PALETTEENTRY* pDestPalette,
    CONST RECT* pDestRect,
    LPCVOID pSrcMemory,
    D3DFORMAT SrcFormat,
    UINT SrcPitch,
    CONST PALETTEENTRY* pSrcPalette,
    CONST RECT* pSrcRect,
    DWORD Filter,
    D3DCOLOR ColorKey)
{
    (void)pDestPalette;
    (void)pSrcPalette;
    (void)Filter;
    (void)ColorKey;

    if (!pDestSurface || !pSrcMemory) {
        XBLog_Write("JA: d3dx8_compat - D3DXLoadSurfaceFromMemory got null input");
        return E_POINTER;
    }

    D3DSURFACE_DESC destDesc;
    if (FAILED(pDestSurface->GetDesc(&destDesc))) {
        XBLog_Write("JA: d3dx8_compat - D3DXLoadSurfaceFromMemory GetDesc failed");
        return E_FAIL;
    }

    RECT destRect = pDestRect ? *pDestRect : MakeFullRect(destDesc.Width, destDesc.Height);
    RECT srcRect = pSrcRect ? *pSrcRect : MakeFullRect(destRect.right - destRect.left, destRect.bottom - destRect.top);

    D3DLOCKED_RECT destLock;
    if (FAILED(pDestSurface->LockRect(&destLock, &destRect, 0))) {
        XBLog_Write("JA: d3dx8_compat - D3DXLoadSurfaceFromMemory LockRect failed");
        return E_FAIL;
    }

    UINT destBpp = BytesPerPixel(destDesc.Format);
    UINT srcBpp = BytesPerPixel(SrcFormat);
    if (!srcBpp && srcRect.right > srcRect.left) {
        srcBpp = SrcPitch / (UINT)(srcRect.right - srcRect.left);
    }
    if (!destBpp || !srcBpp) {
        pDestSurface->UnlockRect();
        return S_OK;
    }

    UINT destWidth = destRect.right - destRect.left;
    UINT destHeight = destRect.bottom - destRect.top;
    UINT srcWidth = srcRect.right - srcRect.left;
    UINT srcHeight = srcRect.bottom - srcRect.top;
    const BYTE* srcBase = static_cast<const BYTE*>(pSrcMemory);
    UINT inferredSrcBpp = srcWidth ? (SrcPitch / srcWidth) : srcBpp;
    if (inferredSrcBpp != 0) {
        srcBpp = inferredSrcBpp;
    }

    for (UINT y = 0; y < destHeight; ++y) {
        UINT srcY = srcHeight ? (y * srcHeight) / destHeight : 0;
        BYTE* destRow = static_cast<BYTE*>(destLock.pBits) + y * destLock.Pitch;
        const BYTE* srcRow = srcBase + srcY * SrcPitch;
        for (UINT x = 0; x < destWidth; ++x) {
            UINT srcX = srcWidth ? (x * srcWidth) / destWidth : 0;
            const BYTE* srcPixel = srcRow + srcX * srcBpp;
            BYTE* destPixel = destRow + x * destBpp;
            WriteColor(destPixel, destDesc.Format, ReadColor(srcPixel, SrcFormat, srcBpp));
        }
    }

    pDestSurface->UnlockRect();
    return S_OK;
}

HRESULT WINAPI D3DXFilterTexture(
    LPDIRECT3DTEXTURE8 pTexture,
    CONST PALETTEENTRY* pPalette,
    UINT SrcLevel,
    DWORD Filter)
{
    (void)pPalette;
    (void)Filter;

    if (!pTexture) {
        XBLog_Write("JA: d3dx8_compat - D3DXFilterTexture got null texture");
        return E_POINTER;
    }

    UINT levelCount = pTexture->GetLevelCount();
    if (levelCount <= 1) {
        return S_OK;
    }

    if (SrcLevel >= levelCount) {
        XBLog_Write("JA: d3dx8_compat - D3DXFilterTexture bad source level");
        return D3DERR_INVALIDCALL;
    }

    for (UINT level = SrcLevel; level + 1 < levelCount; ++level) {
        IDirect3DSurface8* srcSurface = NULL;
        IDirect3DSurface8* dstSurface = NULL;
        D3DSURFACE_DESC srcDesc;
        D3DSURFACE_DESC dstDesc;
        D3DLOCKED_RECT srcLock;
        D3DLOCKED_RECT dstLock;

        if (FAILED(pTexture->GetSurfaceLevel(level, &srcSurface)) ||
            FAILED(pTexture->GetSurfaceLevel(level + 1, &dstSurface))) {
            XBLog_Write("JA: d3dx8_compat - D3DXFilterTexture GetSurfaceLevel failed");
            if (srcSurface) srcSurface->Release();
            if (dstSurface) dstSurface->Release();
            return E_FAIL;
        }

        if (FAILED(srcSurface->GetDesc(&srcDesc)) ||
            FAILED(dstSurface->GetDesc(&dstDesc)) ||
            FAILED(srcSurface->LockRect(&srcLock, NULL, 0)) ||
            FAILED(dstSurface->LockRect(&dstLock, NULL, 0))) {
            XBLog_Write("JA: d3dx8_compat - D3DXFilterTexture surface setup failed");
            if (srcSurface) srcSurface->Release();
            if (dstSurface) dstSurface->Release();
            return E_FAIL;
        }

        UINT srcBpp = BytesPerPixel(srcDesc.Format);
        UINT dstBpp = BytesPerPixel(dstDesc.Format);
        if (!srcBpp || !dstBpp) {
            dstSurface->UnlockRect();
            srcSurface->UnlockRect();
            dstSurface->Release();
            srcSurface->Release();
            continue;
        }

        for (UINT y = 0; y < dstDesc.Height; ++y) {
            BYTE* dstRow = static_cast<BYTE*>(dstLock.pBits) + y * dstLock.Pitch;
            for (UINT x = 0; x < dstDesc.Width; ++x) {
                UINT srcX = x * 2;
                UINT srcY = y * 2;
                unsigned int sumA = 0;
                unsigned int sumR = 0;
                unsigned int sumG = 0;
                unsigned int sumB = 0;
                unsigned int sampleCount = 0;

                for (UINT oy = 0; oy < 2; ++oy) {
                    UINT curY = srcY + oy;
                    if (curY >= srcDesc.Height) {
                        continue;
                    }

                    const BYTE* srcRow = static_cast<const BYTE*>(srcLock.pBits) + curY * srcLock.Pitch;
                    for (UINT ox = 0; ox < 2; ++ox) {
                        UINT curX = srcX + ox;
                        if (curX >= srcDesc.Width) {
                            continue;
                        }

                        D3DCOLOR color = ReadColor(srcRow + curX * srcBpp, srcDesc.Format, srcBpp);
                        sumA += (color >> 24) & 0xff;
                        sumR += (color >> 16) & 0xff;
                        sumG += (color >> 8) & 0xff;
                        sumB += color & 0xff;
                        ++sampleCount;
                    }
                }

                if (!sampleCount) {
                    sampleCount = 1;
                }

                D3DCOLOR averaged = D3DCOLOR_ARGB(
                    sumA / sampleCount,
                    sumR / sampleCount,
                    sumG / sampleCount,
                    sumB / sampleCount);
                WriteColor(dstRow + x * dstBpp, dstDesc.Format, averaged);
            }
        }

        dstSurface->UnlockRect();
        srcSurface->UnlockRect();
        dstSurface->Release();
        srcSurface->Release();
    }

    return S_OK;
}

/* ---- D3DX function wrappers needed by xquake gl_fakegl.cpp -----------
 * The Xbox d3dx8dt.lib pulls in PC GDI/kernel32 deps via cd3dxtext/font/
 * file/resource.  Avoid linking that lib by wrapping the few D3DX math
 * functions FakeGL uses on top of XGMatrix* (in xgraphics.lib, already
 * linked).  D3DXMATRIX layout is identical to XGMATRIX.
 *
 * Important: with _USE_XGMATH defined, xgmath.h has #define
 * D3DXMatrixScaling XGMatrixScaling (and friends).  That would turn our
 * wrapper definitions into recursive self-calls.  Undef them here so we
 * emit real _D3DXMatrixScaling@16 etc. symbols for FakeGL's link. */
#undef D3DXMatrixOrthoOffCenterRH
#undef D3DXMatrixPerspectiveOffCenterRH
#undef D3DXMatrixScaling
#undef D3DXMatrixTranslation

D3DXMATRIX* WINAPI D3DXMatrixOrthoOffCenterRH( D3DXMATRIX *pOut, FLOAT l, FLOAT r, FLOAT b, FLOAT t, FLOAT zn, FLOAT zf )
{
    XGMatrixOrthoOffCenterRH( reinterpret_cast<XGMATRIX*>(pOut), l, r, b, t, zn, zf );
    return pOut;
}

D3DXMATRIX* WINAPI D3DXMatrixPerspectiveOffCenterRH( D3DXMATRIX *pOut, FLOAT l, FLOAT r, FLOAT b, FLOAT t, FLOAT zn, FLOAT zf )
{
    XGMatrixPerspectiveOffCenterRH( reinterpret_cast<XGMATRIX*>(pOut), l, r, b, t, zn, zf );
    return pOut;
}

D3DXMATRIX* WINAPI D3DXMatrixScaling( D3DXMATRIX *pOut, FLOAT sx, FLOAT sy, FLOAT sz )
{
    XGMatrixScaling( reinterpret_cast<XGMATRIX*>(pOut), sx, sy, sz );
    return pOut;
}

D3DXMATRIX* WINAPI D3DXMatrixTranslation( D3DXMATRIX *pOut, FLOAT x, FLOAT y, FLOAT z )
{
    XGMatrixTranslation( reinterpret_cast<XGMATRIX*>(pOut), x, y, z );
    return pOut;
}

/* D3DXMatrixIdentity already comes inline via the local d3dx8.h shim
 * chain — no wrapper needed. */

const char* WINAPI D3DXGetErrorStringA( HRESULT hr, LPSTR pBuffer, UINT BufferLen )
{
    /* FakeGL just logs the result.  Format something simple instead of
     * pulling in full D3DX text formatting (which needs PC GDI). */
    if (pBuffer && BufferLen > 0) {
        _snprintf(pBuffer, BufferLen, "HRESULT 0x%08lX", (unsigned long)hr);
        pBuffer[BufferLen - 1] = '\0';
    }
    return pBuffer;
}

}
