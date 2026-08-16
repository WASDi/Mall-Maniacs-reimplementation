#include <stdio.h>
#include "stubs.h"
#include "gx.h"
#include "util.h"

/* =====================================================================
 * TODO stubs — see stubs.h for contracts. Safe, log-and-return-failure
 * placeholders so future milestones can wire real implementations without
 * changing callers. Original addresses noted in stubs.h.
 * ===================================================================== */

int gameInit(void)
{
    appLog("[stub TODO] gameInit @0x409d90 not implemented (returns 0)");
    return 0;
}

int gameFrameUpdate(void)
{
    appLog("[stub TODO] gameFrameUpdate @0x41abc0 not implemented (returns 0)");
    return 0;
}

int inputPollKeyboard(void)
{
    appLog("[stub TODO] inputPollKeyboard @0x416800 not implemented (returns 0)");
    return 0;
}

void shutdownRenderer(void)
{
    /* Implemented via gx.c (gxUnloadDriver @0x432880). */
    gxShutdown();
}