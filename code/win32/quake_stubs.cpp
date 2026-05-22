/*
 * quake_stubs.cpp
 *
 * Plumbing for xquake's gl_fakegl.cpp — provides the host-engine externs
 * FakeGL expects. Per RENDERER_GRAFT.md: keep small, route to platform.
 *
 * gl_fakegl.cpp expects:
 *   - extern "C" void Con_Printf(char *fmt, ...)
 *   - extern "C" int DIBWidth, DIBHeight
 */

#include <stdio.h>
#include <stdarg.h>

#include "xb_log.h"

extern "C" {

/* DIBWidth/DIBHeight: per RENDERER_GRAFT.md, set to 0 — Xbox path doesn't read them. */
int DIBWidth  = 0;
int DIBHeight = 0;

/* Con_Printf forwards to XBLog so FakeGL diagnostics land in our log file. */
void Con_Printf(char *fmt, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    _vsnprintf(buf, sizeof(buf) - 1, fmt, args);
    va_end(args);
    buf[sizeof(buf) - 1] = '\0';
    XBLog_Write(buf);
}

} /* extern "C" */
