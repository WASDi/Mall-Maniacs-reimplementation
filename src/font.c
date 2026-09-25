#include "compat_types.h"
#include <stdlib.h>
#include <string.h>

#include "font.h"
#include "gx.h"
#include "pool.h"
#include "quest.h"
#include "util.h"

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

/* In-game HUD fonts @0x458364..0x45836c (loaded in roundStartInit). */
gxFont *g_hHudFont;       /* @0x458364 */
gxFont *g_hHudFontDigits; /* @0x458368 */
gxFont *g_hHudFontTiny;   /* @0x45836c */

const char g_szTextTagX[]   = "X";    /* @0x44ed40 */
const char g_szTextTagY[]   = "Y";    /* @0x44ed3c */
const char g_szTextTagRGB[] = "RGB";  /* @0x44f0d4 */
const char g_szTextTagRGB2[]= "RGB2"; /* @0x44f0cc */

/* Glyph quads use the shared GxVert (16 bytes: x, y, z, r, g, b, a) from
 * gx.h. The explicit z=0 / alpha byte normalizes the GXSOFT driver reads at
 * vertex +0x8 and +0xc..+0xf, which the original textDraw left as unwritten
 * stack bytes.
 *
 * The packed UV record must use INTEGER ADDITION, matching the original
 * textDraw @0x409420 (`(ushort)bVar1 * 0x100 + font->pUvx[bVar3]`): using
 * bitwise OR here collapses the texture span whenever the atlas column high
 * byte shares bits with the advance width (e.g. c: U=0x50, gw=0x13 -> OR
 * 0x53 vs ADD 0x63) and reduces row-1 glyphs (V=0x1c00) to a single
 * scanline ((h<<8)|V == V instead of V+h). The driver reads the high byte
 * of each packed u16 (gxDrawPolygon @0x433440 repack, gxDrawTriUV reads). */

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
gxFont *fontParse(char *text, int texture, int posX, int posY, int param5)
{
    gxFont *font;
    char   *p;
    int     i;
    int     squeeze;
    int     posa, posA, pos0;
    int     bLowerA = 0, bUpperA = 0;
    int     atlasPitch;

    font = (gxFont *)memPoolAlloc(g_fontPool, sizeof(gxFont));
    font->nTexture = texture;
    font->nParam5 = param5;

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
gxFont *fontLoad(char *path, int texture, int posX, int posY, int param5)
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
    int          gw;
    int          h;
    unsigned int U;
    unsigned int V;
    GxVert       v0, v1, v2, v3;
    GxColorUv    cuv;

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
        gw = font->pGlyphWidth[b];
        h = (short)font->wPad2;
        U = font->pUvx[b];
        V = font->pUvy[b];
        v0.x = xPos << 8; v0.y = yPos << 8;
        v1.x = (xPos + gw) << 8; v1.y = yPos << 8;
        v2.x = (xPos + gw) << 8; v2.y = (yPos + h) << 8;
        v3.x = xPos << 8; v3.y = (yPos + h) << 8;
        v0.z = v1.z = v2.z = v3.z = 0;
        v0.r = (unsigned char)(g_textColor2 >> 0x10);
        v0.g = (unsigned char)(g_textColor2 >> 8);
        v0.b = (unsigned char)g_textColor2;
        v1.r = v0.r; v1.g = v0.g; v1.b = v0.b;
        v2.r = (unsigned char)(g_textColor >> 0x10);
        v2.g = (unsigned char)(g_textColor >> 8);
        v2.b = (unsigned char)g_textColor;
        v3.r = v2.r; v3.g = v2.g; v3.b = v2.b;
        v0.a = v1.a = v2.a = v3.a = 0;
        cuv.nTexture = font->nTexture;
        cuv.nParam5 = font->nParam5;
        cuv.pad = 0;
        cuv.U = (unsigned short)U;
        cuv.V = (unsigned short)V;
        cuv.gwU = (unsigned short)((gw << 8) + U);
        cuv.V2 = (unsigned short)V;
        cuv.gwU2 = (unsigned short)((gw << 8) + U);
        cuv.hV = (unsigned short)((h << 8) + V);
        cuv.U2 = (unsigned short)U;
        cuv.hV2 = (unsigned short)((h << 8) + V);
        gxDrawPolygon(&v0, &v1, &v2, &v3, color, &cuv);
        xPos = xPos + (int)(short)font->wGlobalSpace + gw;
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

static void formatDecimal(char *buf, int value)
{
    char digits[32];
    int n = 0;
    int negative = value < 0;
    unsigned int magnitude = negative
        ? (unsigned int)(-(value + 1)) + 1u
        : (unsigned int)value;
    do {
        digits[n++] = (char)('0' + magnitude % 10u);
        magnitude /= 10u;
    } while (magnitude != 0);
    if (negative) *buf++ = '-';
    for (int i = 0; i < n; i++) *buf++ = digits[n - 1 - i];
    *buf = '\0';
}

/* textDrawInt @0x4098a0 — draw a decimal integer, returns 1. */
int textDrawInt(void *font, unsigned int color, int x, int y, int value)
{
    char buf[32];

    formatDecimal(buf, value);
    textDraw((gxFont *)font, color, x, y, buf);
    return 1;
}

/* textIntWidth @0x409940 — width of a decimal integer. */
int textIntWidth(void *font, int value)
{
    char buf[32];

    formatDecimal(buf, value);
    return textWidth((gxFont *)font, buf);
}

/* textDrawWrappedCentered @0x4101d0 — word-wrap a quest record's question
 * (QuestRecord.pszQuestion, +0x10 in the 32-bit original) to nMaxWidth px
 * measured with g_hHudFontTiny and draw each line centered at nY, stepping
 * 0xf per line. Wraps on spaces (backtracking to the last space) and hard
 * newlines; the wrap point is temporarily NUL-terminated in place. Called
 * from renderGameHud's frog message panel @0x413692.
 * 64-bit port: the text is read via QuestRecord.pszQuestion, not the raw
 * +0x10 offset (pointer widening moves it to +0x20 on 64-bit; the raw read
 * fetched pPrev instead, so quiz questions never rendered). */
void textDrawWrappedCentered(QuestRecord *pMsg, int nX, int nY, int nMaxWidth)
{
    char *text;
    char cSave;
    char acOne[2];
    int y;
    int w;
    char *pLineEnd;
    char *pBreak;
    char *p;

    (void)nX;
    if (pMsg == NULL) return;
    text = pMsg->pszQuestion;                                  /* +0x10 orig @0x4101e4 */
    y = nY;
    if (text == NULL || *text == '\0') return;
    acOne[1] = '\0';   /* original zero-fills the 1-char measure slot @0x4101f5 */
    for (;;) {
        w = 0;
        pLineEnd = text;
        pBreak = text;
        for (p = text; *p != '\0'; p++) {                /* @0x4101fe */
            if (*p == '\n') break;
            if (p != text) {
                if (*p == '{') {
                    acOne[0] = *p;
                    w += textWidth(g_hHudFontTiny, acOne);
                } else {
                    w += (short)g_hHudFontTiny->wGlobalSpace +
                         (unsigned int)g_hHudFontTiny->pGlyphWidth[(unsigned char)*p];
                }
                if (nMaxWidth <= w) {
                    if (pBreak != text) pLineEnd = pBreak;
                    break;
                }
                if (p[-1] == ' ') pBreak = p - 1;        /* @0x410246 */
            }
        }
        cSave = *pLineEnd;                               /* @0x410262 */
        *pLineEnd = '\0';
        textDrawCentered(g_hHudFontTiny, 0x2004, 0, y, text); /* @0x41026e */
        *pLineEnd = cSave;
        if (*pLineEnd == '\0') return;                   /* @0x41027b */
        text = pLineEnd + 1;                             /* @0x410284 */
        y += 0xf;                                        /* @0x410289 */
    }
}
