// Retired JA MP UI-local header.
//
// Holomatch UI behavior is owned by the shared EF/SP code/ui framework. MP may
// include ui_stefx_spcompat.h for the syscall bridge, or ui_shared.h for the
// dead CGame menu ABI constants used by cg_stefx_ui_shim.c.

#ifndef STEFX_MP_UI_LOCAL_DEAD_H
#define STEFX_MP_UI_LOCAL_DEAD_H

#if defined(STEFX_ELITE_FORCE_MP)
#error codemp/ui/ui_local.h is dead for Holomatch MP; include ui_stefx_spcompat.h or ui_shared.h only.
#else
#error codemp/ui/ui_local.h has been retired in this Xbox tree; restore it only outside Holomatch with an explicit port plan.
#endif

#endif
