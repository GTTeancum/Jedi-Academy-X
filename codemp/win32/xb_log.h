#ifndef XB_LOG_H
#define XB_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the log system. Call once at startup before Com_Printf is used.
// Creates D:\ef_mp_log.txt when writable, otherwise E:\ef_mp_log.txt.
void XBLog_PreCRTProbe(void);
void XBLog_PostCRTProbe(void);
void XBLog_MainProbe(void);
void XBLog_StartupProbe(const char *msg);
void XBLog_Init(void);

// Shutdown: flush and close the log file.
void XBLog_Shutdown(void);

// Write a line to both OutputDebugString and the log file.
// Automatically appends \n if not present.
void XBLog_Write(const char *msg);
void XBLog_Printf(const char *fmt, ...);
void XBLog_Writef(const char *fmt, ...);

#define XBL(msg)  XBLog_Write(msg)
#define XBLF      XBLog_Printf

// Overwrite D:\ef_mp_phase.txt when writable, otherwise E:\ef_mp_phase.txt.
// This keeps a crash breadcrumb without growing the main log every frame.
void XBLog_Phase(const char *msg);

#ifdef __cplusplus
}
#endif

#endif // XB_LOG_H
