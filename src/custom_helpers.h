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

/* Logging / file / TGA helpers (formerly util.c). */
void appLog(const char *fmt, ...);                   /* no original (diag logger) */
void *readFileAlloc(const char *path, size_t *outSize);  /* fileReadRaw-family helper
                                                            for menuInit @0x419c20 */
void *loadTga640x480(const char *path);              /* mirrors tgaLoad16 @0x415df0 */

/* Input + main-loop glue (formerly maniac.c). */
int  vkToKeyId(int vk);                              /* Win32 VK -> game key id (0..7) */
extern int g_bRunning;                               /* main-loop run flag */

/* Menu frame gate (formerly menu.c) — mirrors gameFrameUpdate @0x41a8c0's
 * tail: after the state update the menu states flip + clear. */
void menuFramePost(void);

/* Menu row helpers + intro timeline helpers (extracted from menuUpdate
 * @0x41b0b0 and introUpdate @0x41ae50; no direct original addresses). */
int  menuIsSmallChar(int c);
int  menuRowWidth(const char *label);
void menuRowDraw(const char *label, int y, int bSelected);
void introPresent(int idx);
void introClear(void);

/* Menu tables — custom arrays mirroring original string/texture globals
 * (s_menu_intro_*_tga @0x450794-0x450720, g_hIntroTex* @0x45a618-0x45a62c,
 * row labels @0x450804-0x450828, local_4e0 dispatch table @menuUpdate). */
extern const char * const g_kIntroTga[6];
extern void              *g_hIntroTex[6];
extern const char * const g_kMenuRowLabel[5];
extern PStateFunc         g_kMenuRowTarget[5];

/* Font helpers (extracted from textDraw @0x409420 / textDrawInt @0x4098a0;
 * no direct original addresses). */
void textDrawGlyph(gxFont *font, unsigned int color, unsigned char ch,
                   int *xPos, int yPos);
char *textIntToStr(int value, char buf[32]);

/* shutdownRenderer — original gxUnloadDriver @0x432880 path (see gx.c).
 * Custom name: named shutdown entry point for the full WinMain. */
void shutdownRenderer(void);

#endif /* CUSTOM_HELPERS_H */
