/* test_record_dispatch.c — results-screen record queries must never crash.
 *
 * Regression test for the goal segfault: renderGameHud's results branch
 * (hud.c @0x412993/@0x412b26) calls fmtAtoi(commandDispatch(0, "get
 * vahi<lvl>time<slot>")) / ("get toplevel"). commandDispatch used to
 * return NULL for every record query, so atoi(NULL) segfaulted the first
 * time a round was won. The record table (record.c, decoded from
 * config.mm) now serves these queries, and fmtAtoi tolerates NULL. */

#include "../src/stubs.h"
#include "../src/record.h"
#include "../src/util.h"

int main(void)
{
    const unsigned char *r;

    /* Record time queries are served (never NULL). */
    r = commandDispatch(0, "get vahi0time0");
    if (r == NULL) return 1;
    r = commandDispatch(0, "get fshi2time3");
    if (r == NULL) return 2;

    /* Unknown queries still return NULL, and fmtAtoi tolerates NULL. */
    if (commandDispatch(0, "get frobnicate") != NULL) return 3;
    if (fmtAtoi(NULL) != 0) return 4;

    /* Toplevel set/get round-trip. */
    commandDispatch(0, "set toplevel 3");
    r = commandDispatch(0, "get toplevel");
    if (r == NULL || fmtAtoi((const char *)r) != 3) return 5;
    commandDispatch(0, "set toplevel 4");

    /* Score submit/read-back round-trip ("request vahiscore time slot face lvl"). */
    commandDispatch(0, "request vahiscore 1234 2 7 1");
    r = commandDispatch(0, "get vahi1time2");
    if (r == NULL || fmtAtoi((const char *)r) != 1234) return 6;
    r = commandDispatch(0, "get fshi1time2");
    if (r == NULL || fmtAtoi((const char *)r) != 0) return 7;

    /* Out-of-range indices clamp instead of running off the table. */
    r = commandDispatch(0, "get vahi9time9");
    if (r == NULL) return 8;

    return 0;
}
