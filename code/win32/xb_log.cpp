/*
 * xb_log.cpp  —  Jedi Academy Xbox debug logging
 *
 * Strategy 1: NtCreateFile to raw HDD device paths.
 *   Works on retail hardware and CXBX-R. Bypasses drive-letter symlinks.
 *   \Device\Harddisk0\Partition1\ = E:\ on a standard retail Xbox.
 *
 * Strategy 2: CreateFileA with drive letters.
 *   Fallback for environments where drive letters are already mapped
 *   (dashboard, devkit).
 *
 * Do NOT use XeImageFileName — causes KeBugCheck on CXBX-R.
 */

#ifdef _XBOX
#include <xtl.h>
#else
#include <windows.h>
#endif

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "xb_log.h"

/* ── NT kernel types (minimal subset) ── */
typedef struct { unsigned short Length; unsigned short MaximumLength; char *Buffer; } XBL_STR;
typedef struct { HANDLE RootDirectory; XBL_STR *ObjectName; unsigned long Attributes; } XBL_OA;
typedef struct { union { long Status; void *Pointer; }; unsigned long Information; } XBL_IOSB;

extern "C" long __stdcall NtCreateFile(HANDLE*, unsigned long, XBL_OA*, XBL_IOSB*,
    LARGE_INTEGER*, unsigned long, unsigned long, unsigned long, unsigned long);
extern "C" long __stdcall NtClose(HANDLE);
extern "C" long __stdcall NtWriteFile(HANDLE, HANDLE, void*, void*, XBL_IOSB*,
    void*, unsigned long, LARGE_INTEGER*);

/* ── State ── */
#define XBL_BUF_SIZE 512
static HANDLE g_hLogFile     = INVALID_HANDLE_VALUE;
static int    g_logIsNt      = 0;   /* 1 = NtCreateFile handle, 0 = CreateFileA */
static const char *g_logPath = NULL;

static long xbl_NtCreate(const char *path, HANDLE *out)
{
    XBL_STR  name;
    XBL_OA   oa;
    XBL_IOSB iosb;
    name.Buffer        = (char*)path;
    name.Length        = (unsigned short)strlen(path);
    name.MaximumLength = name.Length + 1;
    oa.RootDirectory   = NULL;
    oa.ObjectName      = &name;
    oa.Attributes      = 0x40;   /* OBJ_CASE_INSENSITIVE */
    return NtCreateFile(out,
        GENERIC_WRITE | 0x00100000,   /* GENERIC_WRITE | SYNCHRONIZE */
        &oa, &iosb, NULL,
        FILE_ATTRIBUTE_NORMAL, 0,
        5,                            /* FILE_OVERWRITE_IF */
        0x20 | 0x02 | 0x40);         /* SYNCHRONOUS_IO_NONALERT | WRITE_THROUGH | NON_DIRECTORY */
}

void XBLog_Init(void)
{
    int  i;
    long status;

    g_hLogFile = INVALID_HANDLE_VALUE;
    g_logIsNt  = 0;
    g_logPath  = NULL;

    /* Strategy 1: NtCreateFile to raw device paths (retail hw + CXBX-R) */
    {
        static const char *ntPaths[] = {
            "\\Device\\Harddisk0\\Partition1\\ja_sp_log.txt",   /* E:\ */
            "\\Device\\Harddisk0\\Partition6\\ja_sp_log.txt",   /* F:\ */
            "\\Device\\Harddisk0\\Partition7\\ja_sp_log.txt",   /* G:\ */
            NULL
        };
        for (i = 0; ntPaths[i]; ++i) {
            status = xbl_NtCreate(ntPaths[i], &g_hLogFile);
            if (status >= 0) {
                g_logIsNt = 1;
                g_logPath = ntPaths[i];
                XBL("=== Jedi Academy Xbox SP log ===\n");
                return;
            }
        }
    }

    /* Strategy 2: CreateFileA with drive letters */
    {
        static const char *caPaths[] = {
            "D:\\ja_sp_log.txt",
            "E:\\ja_sp_log.txt",
            "T:\\ja_sp_log.txt",
            "ja_sp_log.txt",
            NULL
        };
        for (i = 0; caPaths[i]; ++i) {
            g_hLogFile = CreateFileA(caPaths[i], GENERIC_WRITE, FILE_SHARE_READ,
                NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
            if (g_hLogFile != INVALID_HANDLE_VALUE) {
                g_logIsNt = 0;
                g_logPath = caPaths[i];
                XBL("=== Jedi Academy Xbox SP log ===\n");
                return;
            }
        }
    }

    OutputDebugStringA("XBLog_Init: all log paths failed\n");
}

void XBLog_Shutdown(void)
{
    XBL("=== log end ===\n");
    if (g_hLogFile != INVALID_HANDLE_VALUE) {
        if (g_logIsNt) NtClose(g_hLogFile);
        else           CloseHandle(g_hLogFile);
        g_hLogFile = INVALID_HANDLE_VALUE;
    }
    g_logPath = NULL;
}

void XBLog_Print(const char *msg)
{
    DWORD len;
    if (!msg) return;
    OutputDebugStringA(msg);
    if (g_hLogFile == INVALID_HANDLE_VALUE) return;
    len = (DWORD)strlen(msg);
    if (g_logIsNt) {
        XBL_IOSB iosb;
        NtWriteFile(g_hLogFile, NULL, NULL, NULL, &iosb, (void*)msg, len, NULL);
    } else {
        DWORD written;
        WriteFile(g_hLogFile, msg, len, &written, NULL);
    }
}

void XBLog_Printf(const char *fmt, ...)
{
    char    buf[XBL_BUF_SIZE];
    va_list args;
    va_start(args, fmt);
    _vsnprintf(buf, sizeof(buf) - 1, fmt, args);
    va_end(args);
    buf[sizeof(buf) - 1] = '\0';
    XBLog_Print(buf);
}

const char *XBLog_GetPath(void)
{
    return g_logPath;
}

/* Backward-compat: auto-append \n so old call sites don't need changes. */
void XBLog_Write(const char *msg)
{
    XBLog_Printf("%s\n", msg);
}

void XBLog_Writef(const char *fmt, ...)
{
    char    buf[XBL_BUF_SIZE];
    va_list args;
    va_start(args, fmt);
    _vsnprintf(buf, sizeof(buf) - 2, fmt, args);
    va_end(args);
    buf[sizeof(buf) - 2] = '\0';
    /* Append \n so old callers that omit it still get line breaks. */
    int len = (int)strlen(buf);
    buf[len]     = '\n';
    buf[len + 1] = '\0';
    XBLog_Print(buf);
}
