// xb_log.cpp
// File logging for Xbox plus a memory mirror. EF MP avoids OutputDebugString
// during runtime because CXBX-R can fault on the debug-string trap after D3D init.
// Hook: Com_Printf in common.cpp calls XBLog_Write after its normal processing.

#ifdef _XBOX
#include <xtl.h>
#else
#include <windows.h>
#endif
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "xb_log.h"

typedef struct { unsigned short Length; unsigned short MaximumLength; char *Buffer; } XBLogString;
typedef struct { HANDLE RootDirectory; XBLogString *ObjectName; unsigned long Attributes; } XBLogObjectAttributes;
typedef struct { union { long Status; void *Pointer; }; unsigned long Information; } XBLogIoStatusBlock;

extern "C" long __stdcall NtCreateFile(HANDLE*, unsigned long, XBLogObjectAttributes*, XBLogIoStatusBlock*,
    LARGE_INTEGER*, unsigned long, unsigned long, unsigned long, unsigned long);
extern "C" long __stdcall NtClose(HANDLE);
extern "C" long __stdcall NtWriteFile(HANDLE, HANDLE, void*, void*, XBLogIoStatusBlock*,
    void*, unsigned long, LARGE_INTEGER*);
extern "C" long __stdcall NtFlushBuffersFile(HANDLE, XBLogIoStatusBlock*);

static HANDLE g_logFile = INVALID_HANDLE_VALUE;
static HANDLE g_phaseFile = INVALID_HANDLE_VALUE;
static const char *g_logOpenPath = NULL;

extern "C" {
__declspec(dllexport) volatile unsigned int g_XBLogMirrorPos = 0;
__declspec(dllexport) volatile char g_XBLogMirror[32768];
__declspec(dllexport) volatile unsigned int g_XBLogWriteCount = 0;
__declspec(dllexport) volatile char g_XBLogLastLine[512];
__declspec(dllexport) volatile char g_XBLogLastPhase[256];
__declspec(dllexport) volatile unsigned int g_SPXBFakeGLPrimitiveVerts = 0;
__declspec(dllexport) volatile unsigned int g_SPXBFakeGLPrimitiveCalls = 0;
__declspec(dllexport) volatile unsigned int g_SPXBFakeGLStateFlushes = 0;
__declspec(dllexport) volatile unsigned int g_SPXBFramebufferData = 0;
__declspec(dllexport) volatile unsigned int g_SPXBFramebufferPitch = 0;
__declspec(dllexport) volatile unsigned int g_SPXBFramebufferWidth = 0;
__declspec(dllexport) volatile unsigned int g_SPXBFramebufferHeight = 0;
__declspec(dllexport) volatile unsigned int g_SPXBFramebufferFormat = 0;
__declspec(dllexport) volatile unsigned int g_SPXBFramebufferSize = 0;
}

// Prefer the mounted title directory, which is writable on CXBX-R folder
// launches and common softmod installs. Fall back to the title-data partition.
#define XB_LOG_PATH_PRIMARY "D:\\ef_mp_log.txt"
#define XB_LOG_PATH_FALLBACK "E:\\ef_mp_log.txt"
#define XB_PHASE_PATH_PRIMARY "D:\\ef_mp_phase.txt"
#define XB_PHASE_PATH_FALLBACK "E:\\ef_mp_phase.txt"

// Max line length for the formatted output buffer
#define XB_LOG_BUF 2048

static void XBLog_MirrorWrite(const char *text, int len)
{
    if (!text || len <= 0)
    {
        return;
    }

    unsigned int pos = g_XBLogMirrorPos;
    for (int i = 0; i < len; ++i)
    {
        g_XBLogMirror[pos & (sizeof(g_XBLogMirror) - 1)] = text[i];
        ++pos;
    }
    g_XBLogMirror[pos & (sizeof(g_XBLogMirror) - 1)] = 0;
    g_XBLogMirrorPos = pos;
}

static void XBLog_CopyVolatile(volatile char *dest, unsigned int destSize, const char *src)
{
    if (!dest || destSize == 0)
    {
        return;
    }

    unsigned int i = 0;
    if (src)
    {
        while (src[i] && i < destSize - 1)
        {
            dest[i] = src[i];
            ++i;
        }
    }
    dest[i] = 0;
}

static HANDLE XBLog_OpenWritable(const char *primaryPath, const char *fallbackPath, DWORD creationDisposition, bool append, const char **openedPath)
{
    if (openedPath)
    {
        *openedPath = NULL;
    }

    HANDLE h = CreateFileA(primaryPath, GENERIC_WRITE, FILE_SHARE_READ, NULL,
        creationDisposition, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE && openedPath)
    {
        *openedPath = primaryPath;
    }

    if (h == INVALID_HANDLE_VALUE && fallbackPath)
    {
        h = CreateFileA(fallbackPath, GENERIC_WRITE, FILE_SHARE_READ, NULL,
            creationDisposition, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE && openedPath)
        {
            *openedPath = fallbackPath;
        }
    }

    if (h != INVALID_HANDLE_VALUE && append)
    {
        SetFilePointer(h, 0, NULL, FILE_END);
    }

    return h;
}

static void XBLog_RawNtWrite(const char *text, unsigned long len, unsigned long disposition)
{
    static const char path[] = "\\Device\\Harddisk0\\Partition1\\ef_mp_log.txt";
    HANDLE h = INVALID_HANDLE_VALUE;
    XBLogString name;
    XBLogObjectAttributes oa;
    XBLogIoStatusBlock iosb;

    name.Buffer = (char*)path;
    name.Length = sizeof(path) - 1;
    name.MaximumLength = sizeof(path);
    oa.RootDirectory = NULL;
    oa.ObjectName = &name;
    oa.Attributes = 0x40;

    if (NtCreateFile(&h, 0x04 | 0x00100000, &oa, &iosb, NULL,
        FILE_ATTRIBUTE_NORMAL, 0, disposition, 0x20 | 0x02 | 0x40) >= 0)
    {
        NtWriteFile(h, NULL, NULL, NULL, &iosb, (void*)text, len, NULL);
        NtFlushBuffersFile(h, &iosb);
        NtClose(h);
    }
}

extern "C" void XBLog_PreCRTProbe(void)
{
    static const char data[] = "precrt_ok\n";
    XBLog_RawNtWrite(data, sizeof(data) - 1, 5);
}

extern "C" void XBLog_PostCRTProbe(void)
{
    static const char data[] = "post_crt\n";
    XBLog_RawNtWrite(data, sizeof(data) - 1, 3);
}

extern "C" void XBLog_MainProbe(void)
{
    static const char data[] = "main_reached\n";
    XBLog_RawNtWrite(data, sizeof(data) - 1, 3);
}

extern "C" void XBLog_StartupProbe(const char *msg)
{
    unsigned long len = 0;
    char buf[256];

    if (!msg)
    {
        return;
    }

    while (msg[len] && len < sizeof(buf) - 2)
    {
        buf[len] = msg[len];
        ++len;
    }
    buf[len++] = '\n';
    XBLog_RawNtWrite(buf, len, 3);
}

void XBLog_Init(void)
{
    if (g_logFile != INVALID_HANDLE_VALUE)
    {
        CloseHandle(g_logFile);
    }
    g_logFile = XBLog_OpenWritable(XB_LOG_PATH_PRIMARY, XB_LOG_PATH_FALLBACK, CREATE_ALWAYS, false, &g_logOpenPath);
    XBLog_Write("=== Elite Force Holomatch Xbox log started ===");
    XBLog_Printf("STEFX_HM: efmp.xbe runtime log sink path='%s' primary='%s' fallback='%s' build='%s %s'",
        g_logOpenPath ? g_logOpenPath : "raw-nt-partition1",
        XB_LOG_PATH_PRIMARY,
        XB_LOG_PATH_FALLBACK,
        __DATE__,
        __TIME__);
}

void XBLog_Shutdown(void)
{
    if (g_phaseFile != INVALID_HANDLE_VALUE)
    {
        FlushFileBuffers(g_phaseFile);
        CloseHandle(g_phaseFile);
        g_phaseFile = INVALID_HANDLE_VALUE;
    }

    if (g_logFile != INVALID_HANDLE_VALUE)
    {
        XBLog_Write("=== Elite Force Holomatch Xbox log closed ===");
        FlushFileBuffers(g_logFile);
        CloseHandle(g_logFile);
        g_logFile = INVALID_HANDLE_VALUE;
    }
}

void XBLog_Write(const char *msg)
{
    if (!msg || !*msg) return;

    // Build final string with newline
    char buf[XB_LOG_BUF];
    int  len = 0;
    while (msg[len] && len < XB_LOG_BUF - 3)
    {
        buf[len] = msg[len];
        len++;
    }
    // Ensure single \r\n termination for the file; \n for ODS
    if (len > 0 && buf[len-1] == '\n') len--;  // strip trailing \n
    buf[len]   = '\r';
    buf[len+1] = '\n';
    buf[len+2] = '\0';

    XBLog_MirrorWrite(buf, len + 2);
    XBLog_CopyVolatile(g_XBLogLastLine, sizeof(g_XBLogLastLine), msg);
    ++g_XBLogWriteCount;

#if !defined(STEFX_ELITE_FORCE_MP)
    // OutputDebugString goes to CXBX-R console.
    OutputDebugStringA(buf);
#endif

    if (g_logFile == INVALID_HANDLE_VALUE)
    {
        g_logFile = XBLog_OpenWritable(XB_LOG_PATH_PRIMARY, XB_LOG_PATH_FALLBACK, OPEN_ALWAYS, true, &g_logOpenPath);
    }

    if (g_logFile != INVALID_HANDLE_VALUE)
    {
        DWORD written;
        WriteFile(g_logFile, buf, len + 2, &written, NULL);
        // Flush every write so the file is readable even if we crash
        FlushFileBuffers(g_logFile);
    }
    else
    {
        XBLog_RawNtWrite(buf, len + 2, 3);
    }
}

void XBLog_Printf(const char *fmt, ...)
{
    char buf[XB_LOG_BUF];
    va_list args;

    if (!fmt || !*fmt)
    {
        return;
    }

    va_start(args, fmt);
    _vsnprintf(buf, sizeof(buf) - 1, fmt, args);
    va_end(args);
    buf[sizeof(buf) - 1] = 0;

    XBLog_Write(buf);
}

void XBLog_Writef(const char *fmt, ...)
{
    char buf[XB_LOG_BUF];
    va_list args;

    if (!fmt || !*fmt)
    {
        return;
    }

    va_start(args, fmt);
    _vsnprintf(buf, sizeof(buf) - 1, fmt, args);
    va_end(args);
    buf[sizeof(buf) - 1] = 0;

    XBLog_Write(buf);
}

void XBLog_Phase(const char *msg)
{
    if (!msg || !*msg) return;

    XBLog_CopyVolatile(g_XBLogLastPhase, sizeof(g_XBLogLastPhase), msg);

    DWORD now = GetTickCount();
    bool urgent = (strstr(msg, "SEH") || strstr(msg, "exception") || strstr(msg, "invalid") ||
        strstr(msg, "failed") || strstr(msg, "fatal") || strstr(msg, "null") ||
        strstr(msg, "bad idx") || strstr(msg, "SHADER_MAX") || strstr(msg, "no room"));

    static DWORD s_lastPhaseWrite = 0;
    if (!urgent && now - s_lastPhaseWrite < 1000)
    {
        return;
    }

    if (g_phaseFile == INVALID_HANDLE_VALUE)
    {
        g_phaseFile = XBLog_OpenWritable(XB_PHASE_PATH_PRIMARY, XB_PHASE_PATH_FALLBACK, CREATE_ALWAYS, false, NULL);
    }

    if (g_phaseFile == INVALID_HANDLE_VALUE)
    {
        return;
    }

    char buf[256];
    int len = 0;
    while (msg[len] && len < sizeof(buf) - 3)
    {
        buf[len] = msg[len];
        len++;
    }
    if (len > 0 && buf[len - 1] == '\n') len--;
    buf[len++] = '\r';
    buf[len++] = '\n';
    buf[len] = '\0';

    SetFilePointer(g_phaseFile, 0, NULL, FILE_BEGIN);
    DWORD written;
    WriteFile(g_phaseFile, buf, len, &written, NULL);
    SetEndOfFile(g_phaseFile);

    static DWORD s_lastPhaseFlush = 0;
    if (urgent)
    {
        FlushFileBuffers(g_phaseFile);
        s_lastPhaseFlush = now;
    }
    s_lastPhaseWrite = now;
}
