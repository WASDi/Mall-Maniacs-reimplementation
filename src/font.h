#ifndef FONT_H
#define FONT_H

#include <windows.h>

/* Font / text rendering module — reimplementation of maniac font/text
 * functions @0x408f90-0x409940 (see docs/16-rebuild.md "Font / text
 * rendering module" for the full disasm-verified spec).
 *
 * All fonts are loaded from a descriptor .txt (menu\tinyfont.txt etc.) plus
 * a .tpg texture (TINY00.TPG etc.). The descriptor is parsed by fontParse
 * into a 0x510-byte gxFont object allocated from the "FONT" pool
 * (g_fontPool @0x455d3c). */

typedef struct gxFont {
    unsigned short wHeight;            /* +0x00 "width" key */
    unsigned short wPad2;              /* +0x02 "heigth" (sic) key */
    void          *pTexture;           /* +0x04 texture node (colorUv[0]) */
    void          *pParam5;            /* +0x08 (colorUv[1]) */
    unsigned short wGlobalSpace;       /* +0x0c "globalspace" key */
    unsigned char  pGlyphWidth[256];   /* +0x0e "widths" table + defaults */
    unsigned short pUvx[256];          /* +0x10e U texture coord per char */
    unsigned short pUvy[256];          /* +0x30e V texture coord per char */
} gxFont;                              /* 0x510 bytes allocated */

/* Font globals (maniac addresses): glyph map table + text colors. */
extern int           g_fontPool;       /* @0x455d3c */
extern unsigned char g_abFontGlyphMap[256]; /* @0x455d40 */
extern unsigned int  g_textColor;      /* @0x455e40 (bottom color, "RGB2" tag) */
extern unsigned int  g_textColor2;     /* @0x455e44 (top color, "RGB" tag) */

/* Text markup tag names (maniac string addresses). */
extern const char g_szTextTagX[];      /* @0x44ed40 "X" */
extern const char g_szTextTagY[];      /* @0x44ed3c "Y" */
extern const char g_szTextTagRGB[];    /* @0x44f0d4 "RGB" */
extern const char g_szTextTagRGB2[];   /* @0x44f0cc "RGB2" */

/* Font pool lifecycle @0x408f90 / @0x408fc0. */
int fontPoolCreate(void);
int fontPoolDestroy(void);

/* Descriptor parsing helpers @0x408fe0-0x409050 (used by fontParse). */
char *fontDefGetKey(char *text, char *key);
char *fontParseSkipToValue(char *p);
char *fontParseSkipLine(char *p);
char *fontParseSkipSpaces(char *p);

/* Font load / text render @0x409070-0x409940 (default compiler convention). */
gxFont *fontLoad(char *path, void *texture, int posX, int posY, void *param5);
gxFont *fontParse(char *text, void *texture, int posX, int posY, void *param5);
int     textWidth(gxFont *font, char *text);
int     textDraw(gxFont *font, unsigned int color, int x, int y, char *text);
int     textDrawCentered(gxFont *font, unsigned int color, int x, int y, char *text);
int     textDrawInt(void *font, unsigned int color, int x, int y, int value);
int     textIntWidth(void *font, int value);
void    textDrawWrappedCentered(void *pMsgBlock, int nX, int nY, int nMaxWidth); /* @0x4101d0 */

/* In-game HUD fonts @0x458364..0x45836c, loaded by roundStartInit
 * @0x40a727..0x40a7f2 from the per-level HUD directory
 * (g_aszLevelDirs[g_nLevelIdx]). */
extern gxFont *g_hHudFont;       /* @0x458364 font.txt        + font00.tpg (list/scroll text) */
extern gxFont *g_hHudFontDigits; /* @0x458368 hudfont.txt     + hfont00.tpg (counts, timer) */
extern gxFont *g_hHudFontTiny;   /* @0x45836c menu\tinyfont.txt + tfont00.tpg (labels) */

#endif /* FONT_H */
