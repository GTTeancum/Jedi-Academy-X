// xb_log.cpp
// Dual-output logging for Xbox: OutputDebugString (CXBX-R console) + D:\ja_mp_log.txt (hardware)
// Hook: Com_Printf in common.cpp calls XBLog_Write after its normal processing.

#ifdef _XBOX
#include <xtl.h>
#else
#include <windows.h>
#include <stdio.h>
#endif

#include "xb_log.h"

typedef struct { unsigned short Length; unsigned short MaximumLength; char *Buffer; } XBLogString;
typedef struct { HANDLE RootDirectory; XBLogString *ObjectName; unsigned long Attributes; } XBLogObjectAttributes;
typedef struct { union { long Status; void *Pointer; }; unsigned long Information; } XBLogIoStatusBlock;

extern "C" long __stdcall NtCreateFile(HANDLE*, unsigned long, XBLogObjectAttributes*, XBLogIoStatusBlock*,
    LARGE_INTEGER*, unsigned long, unsigned long, unsigned long, unsigned long);
extern "C" long __stdcall NtClose(HANDLE);
extern "C" long __stdcall NtWriteFile(HANDLE, HANDLE, void*, void*, XBLogIoStatusBlock*,
    void*, unsigned long, LARGE_INTEGER*);

static HANDLE g_logFile = INVALID_HANDLE_VALUE;

// Softmod launchers mount the active game root as D:\, which leaves the log
// beside the running XBE and makes it easy to retrieve after a crash.
#define XB_LOG_PATH "D:\\ja_mp_log.txt"

// Max line length for the formatted output buffer
#define XB_LOG_BUF 2048

static void XBLog_RawNtWrite(const char *text, unsigned long len, unsigned long disposition)
{
    static const char path[] = "\\Device\\Harddisk0\\Partition1\\ja_mp_log.txt";
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
    g_logFile = CreateFileA(XB_LOG_PATH, GENERIC_WRITE, FILE_SHARE_READ, NULL,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    XBLog_Write("=== Jedi Academy Xbox log started ===");
}

void XBLog_Shutdown(void)
{
    if (g_logFile != INVALID_HANDLE_VALUE)
    {
        XBLog_Write("=== Jedi Academy Xbox log closed ===");
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

    // OutputDebugString goes to CXBX-R console
    OutputDebugStringA(buf);

    if (g_logFile == INVALID_HANDLE_VALUE)
    {
        g_logFile = CreateFileA(XB_LOG_PATH, GENERIC_WRITE, FILE_SHARE_READ, NULL,
            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (g_logFile != INVALID_HANDLE_VALUE)
        {
            SetFilePointer(g_logFile, 0, NULL, FILE_END);
        }
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
