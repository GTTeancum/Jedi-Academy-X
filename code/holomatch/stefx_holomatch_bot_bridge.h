#ifndef STEFX_HOLOMATCH_BOT_BRIDGE_H
#define STEFX_HOLOMATCH_BOT_BRIDGE_H

#include <stdarg.h>

int STEFX_HolomatchBotSyscall(int command, va_list args, int *result);
void STEFX_HolomatchBotReset(void);

#endif
