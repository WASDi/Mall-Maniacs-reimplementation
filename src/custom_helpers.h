#ifndef CUSTOM_HELPERS_H
#define CUSTOM_HELPERS_H

#include <windows.h>
#include <stddef.h>

#include "menu.h"
#include "font.h"

/* =====================================================================
 * Custom helpers — rebuild-only functions/symbols that have NO counterpart
 * in Ghidra's maniac.exe. Kept in custom_helpers.c so each subsystem file
 * contains only Ghidra-mapped code (per Rebuild.md: custom code is clearly
 * labeled; original addresses noted where one is mirrored, and marked
 * "no direct original address" otherwise).
 * ===================================================================== */

/* Logging helper. */
void appLog(const char *fmt, ...);                   /* no original (diag logger) */



#endif /* CUSTOM_HELPERS_H */
