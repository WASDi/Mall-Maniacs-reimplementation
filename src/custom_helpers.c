#include <stdio.h>
#include <stdarg.h>
#include <windows.h>

#include "custom_helpers.h"
#include "stubs.h"

/* =====================================================================
 * Custom helpers — rebuild-only code with NO counterpart in Ghidra's
 * maniac.exe. See custom_helpers.h for the full contract. Original
 * addresses are noted where one is mirrored; otherwise "no direct
 * original address".
 * ===================================================================== */

/* --- rebuild diagnostics --- */

/* appLog — rebuild diagnostic helper, appends to "rebuild.log". No original
 * in maniac.exe (replacement logging used by the vertical slice). */
void appLog(const char *fmt, ...)
{
    FILE *f = fopen("rebuild.log", "a");
    va_list ap;
    if (f == NULL) return;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fprintf(f, "\n");
    fclose(f);
}
