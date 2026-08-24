#include <stdio.h>
#include <stdarg.h>
#include <windows.h>

#include "custom_helpers.h"

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

/* setSignVerts — rebuild-only util (see custom_helpers.h). Sets the four
 * sign-quad vertices to z = 0, r = g = b = 0xff, reproducing the common
 * fields the original gameFrameUpdate @0x41aba5 color loop applies inline to
 * each sign quad. */
void setSignVerts(GxVert *v0, GxVert *v1, GxVert *v2, GxVert *v3)
{
    v0->z = v1->z = v2->z = v3->z = 0;
    v0->r = v0->g = v0->b = 0xff;
    v1->r = v1->g = v1->b = 0xff;
    v2->r = v2->g = v2->b = 0xff;
    v3->r = v3->g = v3->b = 0xff;
}
