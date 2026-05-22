#ifndef XB_LOG_H
#define XB_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the log system. Call once at startup before Com_Printf is used.
// Creates D:\ja_mp_log.txt on Xbox hardware.
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

#ifdef __cplusplus
}
#endif

#endif // XB_LOG_H
