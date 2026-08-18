#include <windows.h>
#include <stdlib.h>
#include <string.h>

#include "font.h"
#include "gx.h"
#include "pool.h"
#include "util.h"
#include "custom_helpers.h"

/* =====================================================================
 * Font / text rendering module — reimplementation of maniac font/text
 * functions @0x408f90-0x409940 (see docs/16-rebuild.md). Verified against
 * the raw disassembly; the Ghidra decompiler is correct on all points.
 * Numeric conversion uses the CRT-compatible strtol substitution for
 * fmtParseInt @0x43e860; the original parser is a statically linked utility
 * outside the rebuild scope.
 *
 *   fontPoolCreate/destroy  @0x408f90 / @0x408fc0
 *   fontDefGetKey           @0x408fe0  = strstr(text, key)
 *   fontParseSkipToValue    @0x409000  skip to '=' + 1
 *   fontParseSkipLine       @0x409020  skip to end of line + newlines
 *   fontParseSkipSpaces     @0x409050  skip ' ' runs
 *   fontParse               @0x4090c0  descriptor -> gxFont (pool 0x510)
 *   fontLoad                @0x409070  fileReadText(0, path) + fontParse
 *   textWidth               @0x409810
 *   textDraw                @0x409420
 *   textDrawCentered        @0x409860
 *   textDrawInt             @0x4098a0  (itoa + textDraw, returns 1)
 *   textIntWidth            @0x409940  (itoa + textWidth)
 * ===================================================================== */

int           g_fontPool;             /* @0x455d3c */
unsigned char g_abFontGlyphMap[256];  /* @0x455d40 */
unsigned int  g_textColor;            /* @0x455e40 (bottom / "RGB2" tag) */
unsigned int  g_textColor2;           /* @0x455e44 (top / "RGB" tag) */

const char g_szTextTagX[]   = "X";    /* @0x44ed40 */
const char g_szTextTagY[]   = "Y";    /* @0x44ed3c */
const char g_szTextTagRGB[] = "RGB";  /* @0x44f0d4 */
const char g_szTextTagRGB2[]= "RGB2"; /* @0x44f0cc */

/* fontPoolCreate @0x408f90 — create the "FONT" pool, reset text colors. */
int fontPoolCreate(void)
{
    g_fontPool = memPoolCreate("FONT");
    g_textColor2 = 0xffffff;
    g_textColor = 0xffffff;
    return 1;
}

/* fontPoolDestroy @0x408fc0 */
int fontPoolDestroy(void)
{
    memPoolDestroy(g_fontPool);
    return 1;
}

/* fontDefGetKey @0x408fe0 — thin wrapper over strFindSubstring (strstr). */
char *fontDefGetKey(char *text, char *key)
{
    return strstr(text, key);
}

/* fontParseSkipToValue @0x409000 — advance to '=', return ptr past it. */
char *fontParseSkipToValue(char *p)
{
    while (*p != '=') p = p + 1;
    return p + 1;
}

/* fontParseSkipLine @0x409020 — advance past end of line, then all \n\r. */
char *fontParseSkipLine(char *p)
{
    while (*p != '\n' && *p != '\r') p = p + 1;
    while (*p == '\n' || *p == '\r') p = p + 1;
    return p;
}

/* fontParseSkipSpaces @0x409050 */
char *fontParseSkipSpaces(char *p)
{
    while (*p == ' ') p = p + 1;
    return p;
}

/* fontParse @0x4090c0 — parse a font descriptor into a gxFont object.
 *
 * Descriptor format (CRLF, latin-1, see menu\tinyfont.txt):
 *   width = 14 ; heigth = 16 ; posa = 29 ; posA = 0 ; pos0 = 64 ;
 *   spacewidth = 6 ; globalspace = 1 ; squeeze = -10 ; spacepos = 0 ;
 *   positions{ <byte> <glyph> ... }
 *   widths{    <byte> <width> ... }
 * Keys are located via strstr from the start of text each time. */
gxFont *fontParse(char *text, void *texture, int posX, int posY, void *param5)
{
    gxFont *font;
    char   *p;
    int     i;
    int     squeeze;
    int     posa, posA, pos0;
    int     bLowerA = 0, bUpperA = 0;
    int     atlasPitch;

    font = (gxFont *)memPoolAlloc(g_fontPool, 0x510);
    font->pTexture = texture;
    font->pParam5 = param5;

    /* fmtParseInt @0x43e860 is the original static-library decimal parser;
     * strtol preserves its result for the shipped descriptor grammar. */
    p = fontDefGetKey(text, "width");
    p = fontParseSkipToValue(p);
    font->wHeight = (unsigned short)strtol(p, NULL, 10);

    p = fontDefGetKey(text, "heigth");
    p = fontParseSkipToValue(p);
    font->wPad2 = (unsigned short)strtol(p, NULL, 10);

    p = fontDefGetKey(text, "squeeze");
    p = fontParseSkipToValue(p);
    squeeze = (short)strtol(p, NULL, 10);

    p = fontDefGetKey(text, "globalspace");
    p = fontParseSkipToValue(p);
    font->wGlobalSpace = (unsigned short)strtol(p, NULL, 10);

    for (i = 0; i < 0x100; i++) {
        g_abFontGlyphMap[i] = 0;
        font->pGlyphWidth[i] = (unsigned char)font->wHeight;
    }

    p = fontDefGetKey(text, "posa");
    p = fontParseSkipToValue(p);
    posa = (int)strtol(p, NULL, 10);
    for (i = 'a'; i <= 'z'; i++) {
        g_abFontGlyphMap[i] = (unsigned char)(i - 'a' + posa);
    }

    p = fontDefGetKey(text, "posA");
    p = fontParseSkipToValue(p);
    posA = (int)strtol(p, NULL, 10);
    for (i = 'A'; i <= 'Z'; i++) {
        g_abFontGlyphMap[i] = (unsigned char)(i - 'A' + posA);
    }

    p = fontDefGetKey(text, "pos0");
    p = fontParseSkipToValue(p);
    pos0 = (int)strtol(p, NULL, 10);
    for (i = '0'; i <= '9'; i++) {
        g_abFontGlyphMap[i] = (unsigned char)(i - '0' + pos0);
    }

    p = fontDefGetKey(text, "positions{");
    p = fontParseSkipLine(p);
    p = fontParseSkipSpaces(p);
    while (*p != '}') {
        unsigned char ch = (unsigned char)*p;
        p = p + 1;
        g_abFontGlyphMap[ch] = (unsigned char)strtol(p, NULL, 10);
        p = fontParseSkipLine(p);
        p = fontParseSkipSpaces(p);
    }

    p = fontDefGetKey(text, "widths{");
    p = fontParseSkipLine(p);
    p = fontParseSkipSpaces(p);
    while (*p != '}') {
        unsigned char ch = (unsigned char)*p;
        p = p + 1;
        font->pGlyphWidth[ch] = (unsigned char)strtol(p, NULL, 10);
        if (ch == 'a') bLowerA = 1;
        else if (ch == 'A') bUpperA = 1;
        p = fontParseSkipLine(p);
        p = fontParseSkipSpaces(p);
    }

    if (posa == posA) {
        if (bLowerA == 1 && bUpperA == 0) {
            for (i = 'A'; i <= 'Z'; i++) {
                font->pGlyphWidth[i] = font->pGlyphWidth[i + 0x20];
            }
        } else if (bLowerA == 0 && bUpperA == 1) {
            for (i = 'a'; i <= 'z'; i++) {
                font->pGlyphWidth[i] = font->pGlyphWidth[i - 0x20];
            }
        }
    }

    p = fontDefGetKey(text, "spacewidth");
    p = fontParseSkipToValue(p);
    font->pGlyphWidth[0x20] = (unsigned char)strtol(p, NULL, 10);

    p = fontDefGetKey(text, "spacepos");
    p = fontParseSkipToValue(p);
    g_abFontGlyphMap[0x20] = (unsigned char)strtol(p, NULL, 10);

    atlasPitch = (0x100 / (int)(short)font->wHeight + squeeze) * (int)(short)font->wHeight;
    for (i = 0; i < 0x100; i++) {
        int row = (int)(unsigned char)g_abFontGlyphMap[i] * (int)(short)font->wHeight;
        int page = 0;
        if (row >= atlasPitch) {
            do {
                row -= atlasPitch;
                page += (int)(short)font->wPad2;
            } while (row >= atlasPitch);
        }
        font->pUvx[i] = (unsigned short)((row + posX) << 8);
        font->pUvy[i] = (unsigned short)((posY + page) << 8);
    }

    return font;
}

/* fontLoad @0x409070 — read descriptor file (pool 0) and parse it. */
gxFont *fontLoad(char *path, void *texture, int posX, int posY, void *param5)
{
    char   *text;
    gxFont *font;

    text = fileReadText(0, path);
    if (text == NULL) return NULL;

    font = fontParse(text, texture, posX, posY, param5);
    memPoolFree(0, text);
    return font;
}

/* textWidth @0x409810 — measure rendered width; tags ({..}) add nothing. */
int textWidth(gxFont *font, char *text)
{
    int i = 0;
    int w = 0;

    if (*text == '\0') return 0;
    do {
        if (text[i] == '{') {
            int j;
            do {
                j = i;
                i = j + 1;
            } while (text[j + 1] != '}');
            i = j + 2;
        }
        w = (short)font->wGlobalSpace + w + (unsigned int)font->pGlyphWidth[(unsigned char)text[i]];
        i = i + 1;
    } while (text[i] != '\0');
    return w;
}

/* textDraw @0x409420 — render text with inline {Name:Value;} tags.
 *   {X:n}/{Y:n} decimal -> move cursor; {RGB:h}/{RGB2:h} hex -> colors.
 *   "{{" renders a literal '{'. Returns the final x cursor. */
int textDraw(gxFont *font, unsigned int color, int x, int y, char *text)
{
    unsigned int uSaved1 = g_textColor;
    unsigned int uSaved2 = g_textColor2;
    int          xPos = x;
    int          yPos = y;
    int          i = 0;
    char         c = *text;
    unsigned char b;

    for (;;) {
        if (c == '\0') {
            g_textColor = uSaved1;
            g_textColor2 = uSaved2;
            return xPos;
        }
        b = (unsigned char)text[i];
        if (b == 0x20) {
            xPos = (short)font->wGlobalSpace + xPos + (unsigned int)font->pGlyphWidth[0x20];
        } else if (b == 0x7b) {          /* '{' */
            c = text[i + 1];
            i = i + 1;
            if (c == '{') {
                goto draw_glyph;
            } else {
                while (c != '}') {
                    char name[16];
                    char value[32];
                    int  n = 0;
                    int  v = 0;
                    memset(name, 0, sizeof name);
                    memset(value, 0, sizeof value);
                    if (c != ':') {
                        do {
                            name[n++] = c;
                            i = i + 1;
                            c = text[i];
                        } while (c != ':');
                    }
                    i = i + 1;
                    b = (unsigned char)text[i];
                    if (b != 0x3b) {     /* ';' */
                        do {
                            value[v] = (char)b;
                            b = (unsigned char)text[i + 1];
                            v = v + 1;
                            i = i + 1;
                        } while (b != 0x3b);
                    }
                    i = i + 1;
                    if (strcmp(name, g_szTextTagX) == 0) {
                        xPos = (int)strtol(value, NULL, 10);
                    } else if (strcmp(name, g_szTextTagY) == 0) {
                        yPos = (int)strtol(value, NULL, 10);
                    } else if (strcmp(name, g_szTextTagRGB) == 0) {
                        g_textColor2 = (unsigned int)strtol(value, NULL, 16);
                    } else if (strcmp(name, g_szTextTagRGB2) == 0) {
                        g_textColor = (unsigned int)strtol(value, NULL, 16);
                    }
                    c = text[i];
                }
            }
        } else if (b != 0) {
            goto draw_glyph;
        }
        c = text[i + 1];
        i = i + 1;
        continue;

    draw_glyph:
        textDrawGlyph(font, color, b, &xPos, yPos);
        c = text[i + 1];
        i = i + 1;
    }
}

/* textDrawCentered @0x409860 — center on a 640px HUD space. */
int textDrawCentered(gxFont *font, unsigned int color, int x, int y, char *text)
{
    int w;

    w = textWidth(font, text);
    return textDraw(font, color, (x - w / 2) + 0x140, y, text);
}

/* textDrawInt @0x4098a0 — draw a decimal integer, returns 1. */
int textDrawInt(void *font, unsigned int color, int x, int y, int value)
{
    char buf[32];

    textIntToStr(value, buf);
    textDraw((gxFont *)font, color, x, y, buf);
    return 1;
}

/* textIntWidth @0x409940 — width of a decimal integer. */
int textIntWidth(void *font, int value)
{
    char buf[32];

    textIntToStr(value, buf);
    return textWidth((gxFont *)font, buf);
}
