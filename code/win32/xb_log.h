/*
 * xb_log.h  —  Jedi Academy Xbox debug logging
 *
 * Usage:
 *   XBL("Win_Init: device ready\n");
 *   XBLF("renderer: %d textures loaded\n", count);
 *
 * Output goes to:
 *   - OutputDebugStringA  (visible in CXBX-Reloaded / xemu GDB)
 *   - \Device\Harddisk0\Partition1\ja_sp_log.txt  (E:\ root on retail hardware)
 *
 * Call XBLog_Init() once at the top of main() before any XBL usage.
 * Call XBLog_Shutdown() on exit.
 */

#ifndef XB_LOG_H
#define XB_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

void XBLog_Init(void);
void XBLog_Shutdown(void);
void XBLog_Print(const char *msg);
void XBLog_Printf(const char *fmt, ...);
const char *XBLog_GetPath(void);

/* Convenience macros — VC71 C89 mode doesn't support __VA_ARGS__,
   so XBLF is a direct function alias rather than a variadic macro. */
#define XBL(msg)  XBLog_Print(msg)
#define XBLF      XBLog_Printf

/* ── Backward-compat shims for existing call sites ─────────────────────
   XBLog_Write/XBLog_Writef auto-append \n so old callers don't need to
   change.  New code should use XBL("msg\n") / XBLF("fmt\n", ...).     */
void XBLog_Write(const char *msg);
void XBLog_Writef(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* XB_LOG_H */
