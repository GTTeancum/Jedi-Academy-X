#ifndef XB_LOG_MP_COMPAT_H
#define XB_LOG_MP_COMPAT_H

#include "xb_log.h"

#ifdef __cplusplus
extern "C" {
#endif

void XBLog_MainProbe(void);
void XBLog_StartupProbe(const char *msg);
void XBLog_Phase(const char *msg);

#ifdef __cplusplus
}
#endif

#endif
