#ifndef CUSTOM_HELPERS_H
#define CUSTOM_HELPERS_H

#include <windows.h>
#include <stddef.h>

#include "menu.h"
#include "font.h"

/* =====================================================================
 * Custom helpers — rebuild-only functions/symbols that have NO counterpart
 * in Ghidra's maniac.exe. Kept in custom_helpers.c so each subsystem file
 * contains only Ghidra-mapped code (custom code is clearly
 * labeled; original addresses noted where one is mirrored, and marked
 * "no direct original address" otherwise).
 * ===================================================================== */

/* Logging helper. */
void appLog(const char *fmt, ...);                   /* no original (diag logger) */

/* Sign-quad vertex common fields (gameFrameUpdate @0x41aba5 color loop):
 * z = 0 and r = g = b = 0xff on all four vertices. A rebuild-only util shared
 * by the menu/options/record/charselect sign quads. */
void setSignVerts(GxVert *v0, GxVert *v1, GxVert *v2, GxVert *v3);  /* no original */

#endif /* CUSTOM_HELPERS_H */

