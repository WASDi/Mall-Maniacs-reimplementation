#include <stdio.h>
#include "stubs.h"
#include "gx.h"
#include "util.h"

/* =====================================================================
 * TODO stubs — see stubs.h for contracts. Safe, log-and-return-failure
 * placeholders so future milestones can wire real implementations without
 * changing callers. Original addresses noted in stubs.h.
 * ===================================================================== */

/* gameInit @0x409d90 — full game init. TODO stub: log + return 0. */
int gameInit(void)
{
    appLog("[stub TODO] gameInit @0x409d90 not implemented (returns 0)");
    return 0;
}

/* gameFrameUpdate @0x41a8c0 — advance one game frame. TODO stub. */
int gameFrameUpdate(void)
{
    appLog("[stub TODO] gameFrameUpdate @0x41a8c0 not implemented (returns 0)");
    return 0;
}

/* pollKeyboard @0x416a10 — DirectInput keyboard poll. TODO stub; the slice
 * uses window messages instead. */
int pollKeyboard(void)
{
    appLog("[stub TODO] pollKeyboard @0x416a10 not implemented (returns 0)");
    return 0;
}

/* shutdownRenderer — original gxUnloadDriver @0x432880 path (see gx.c). */
void shutdownRenderer(void)
{
    /* Implemented via gx.c (gxUnloadDriver @0x432880). */
    gxShutdown();
}