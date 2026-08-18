#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stddef.h>
#include <windows.h>

#include "custom_helpers.h"
#include "gx.h"
#include "stubs.h"

/* =====================================================================
 * Custom helpers — rebuild-only code with NO counterpart in Ghidra's
 * maniac.exe. See custom_helpers.h for the full contract. Original
 * addresses are noted where one is mirrored; otherwise "no direct
 * original address".
 * ===================================================================== */

/* --- logging / file / TGA helpers (formerly util.c) --- */

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

/* readFileAlloc — malloc'd whole-file read. No direct original address
 * (replacement helper for the fileReadRaw family used by menuInit @0x419c20). */
void *readFileAlloc(const char *path, size_t *outSize)
{
    FILE *f;
    long len;
    void *buf;

    *outSize = 0;
    f = fopen(path, "rb");
    if (f == NULL) {
        appLog("[file] open failed: %s", path);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    len = ftell(f);
    if (len <= 0) { fclose(f); return NULL; }
    rewind(f);
    buf = malloc((size_t)len);
    if (buf == NULL) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf); fclose(f); return NULL;
    }
    fclose(f);
    *outSize = (size_t)len;
    return buf;
}

/* loadTga640x480 — mirrors tgaLoad16 @0x415df0: 640x480 8-bit indexed
 * (type 1) or grayscale (type 3) TGA into a malloc'd 0x4b000 index buffer.
 * Pixel-data offset = 18 + idlen + (colormap_length * colormap_depth / 8);
 * for intro_addgames.tga: 18 + 18 + (256*24/8) = 804, 307200 bytes. */
void *loadTga640x480(const char *path)
{
    unsigned char hdr[18];
    size_t size = 0;
    unsigned char *buf, *pix;
    unsigned int offset;
    unsigned int w, h;
    unsigned int i;

    buf = (unsigned char *)readFileAlloc(path, &size);
    if (buf == NULL) return NULL;
    if (size < 18) { appLog("[tga] %s too small", path); free(buf); return NULL; }

    memcpy(hdr, buf, 18);
    /* byte[2]: image type. type 1 = color-mapped, type 3 = grayscale.
     * We accept 8-bit data and copy indices verbatim, as tgaLoad16 does. */
    w = hdr[12] | (hdr[13] << 8);
    h = hdr[14] | (hdr[15] << 8);
    if (w != 0x280 || h != 0x1e0) {
        appLog("[tga] %s unsupported size %ux%u (want 640x480)", path, w, h);
        free(buf);
        return NULL;
    }

    offset = 18u + (unsigned int)hdr[0] +
             ((unsigned int)(hdr[7]) * (hdr[5] | (hdr[6] << 8)) / 8u);
    if (offset + 0x4b000 > size) {
        appLog("[tga] %s truncated (offset %u + 0x4b000 > %u)", path, offset, (unsigned)size);
        free(buf);
        return NULL;
    }

    pix = (unsigned char *)malloc(0x4b000);
    if (pix == NULL) { free(buf); return NULL; }
    memcpy(pix, buf + offset, 0x4b000);
    free(buf);

    /* Flip vertically if the descriptor bit 0x20 (origin, top-left) is clear. */
    if ((hdr[17] & 0x20) == 0) {
        unsigned char tmp[0x280];
        for (i = 0; i < 0x1e0 / 2u; i++) {
            memcpy(tmp, pix + i * 0x280, 0x280);
            memcpy(pix + i * 0x280, pix + (0x1e0 - 1u - i) * 0x280, 0x280);
            memcpy(pix + (0x1e0 - 1u - i) * 0x280, tmp, 0x280);
        }
    }
    return pix;
}

/* --- input + main-loop glue (formerly maniac.c) --- */

/* Map Win32 virtual key -> game key id (Key id map, docs/12-input.md:
 * 0=Right, 1=Left, 2=Up, 3=Down, 4=Space, 6=Enter, 7=Esc). Unknown keys
 * report as -1 so they remain ignored by states that have no such input. */
int vkToKeyId(int vk)
{
    switch (vk) {
    case VK_RIGHT:  return 0;
    case VK_LEFT:   return 1;
    case VK_UP:     return 2;
    case VK_DOWN:   return 3;
    case VK_SPACE:  return 4;
    case VK_RETURN: return 6;
    case VK_ESCAPE: return 7;
    default:        return -1;  /* no original game key for this VK */
    }
}

/* g_bRunning — main-loop run flag. No direct original address (custom). */
int g_bRunning = 1;

/* --- menu frame gate + row/intro helpers (formerly menu.c) --- */

/* menuFramePost — mirrors gameFrameUpdate @0x41a8c0's tail: after the state
 * update the menu states flip + clear. introUpdate and stateQuitConfirm are
 * excluded because they present their own frames. */
void menuFramePost(void)
{
    if (g_pStateFunc != introUpdate && g_pStateFunc != stateQuitConfirm) {
        gxFlip();
        gxClearScreen(1, 0);   /* g_nClearColor @0x45892c = 0 */
    }
}

/* Intro logos in load/present order — mirrors menuInit @0x419c20 and the
 * maniac globals g_hIntroTexAddgames..g_hIntroTexPresenterar @0x45a618-0x45a62c
 * (original string table s_menu_intro_*_tga @0x450794-0x450720). */
const char * const g_kIntroTga[6] = {
    "menu\\intro_addgames.tga",    /* g_hIntroTexAddgames   @0x45a618 */
    "menu\\intro_och.tga",         /* g_hIntroTexOch        @0x45a61c */
    "menu\\intro_uds.tga",         /* g_hIntroTexUds        @0x45a620 */
    "menu\\intro_samarbete.tga",   /* g_hIntroTexSamarbete  @0x45a624 */
    "menu\\intro_mcd.tga",         /* g_hIntroTexMcd        @0x45a628 */
    "menu\\intro_presenterar.tga"  /* g_hIntroTexPresenterar @0x45a62c */
};
void *g_hIntroTex[6];

/* Main-menu rows (menuUpdate @0x41b0b0). Labels mirror the .rdata strings
 * s_Spela @0x450828 / "Nätverk" @0x450820 / s_Alternativ @0x450814 /
 * s_Rekord @0x45080c / s_Avsluta @0x450804. Targets mirror the local_4e0
 * dispatch table; the first four are stubs declared in stubs.h. */
const char * const g_kMenuRowLabel[5] = {
    "Spela", "N\xe4tverk", "Alternativ", "Rekord", "Avsluta"
};
PStateFunc g_kMenuRowTarget[5] = {
    stateGameTypeSelect, stateNetworkMenu, gotoOptions, stateHighScoreTable,
    stateQuitConfirm
};

/* menuUpdate @0x41b0b0 stream splitter: a-z plus the Swedish lowercase
 * vowels å/ä/ö (0xe5/0xe4/0xf6) are drawn with the small menu font; every
 * other character (uppercase, digits, space) with the large font. */
int menuIsSmallChar(int c)
{
    unsigned int u = (unsigned char)c;
    return (u > 0x60 && u < 0x7b) || u == 0xe5 || u == 0xe4 || u == 0xf6;
}

/* Measure one menu row. The small-font and large-font tokens are measured
 * with g_hMenuFontSmall / g_hMenuFont (menuUpdate @0x41b0b0 width passes;
 * the "200" highlight fonts share the same descriptor so their widths match). */
int menuRowWidth(const char *label)
{
    char         tok[128];
    int          w = 0;
    const char  *p = label;

    while (*p != '\0') {
        const char *q;
        int         n;

        q = p;
        while (*q != '\0' && menuIsSmallChar((unsigned char)*q)) q = q + 1;
        n = (int)(q - p);
        if (n > 0) {
            memcpy(tok, p, (size_t)n); tok[n] = '\0';
            w += textWidth(g_hMenuFontSmall, tok);
            p = q;
            continue;
        }
        q = p;
        while (*q != '\0' && !menuIsSmallChar((unsigned char)*q)) q = q + 1;
        n = (int)(q - p);
        if (n > 0) {
            memcpy(tok, p, (size_t)n); tok[n] = '\0';
            w += textWidth(g_hMenuFont, tok);
            p = q;
        }
    }
    return w;
}

/* Draw one menu row centered on x = 0x226 at the given y. Selected row uses
 * the "200" highlight fonts g_hMenuMsfnt/g_hMenuMfnt, unselected rows
 * g_hMenuFontSmall/g_hMenuFont; both at color 0x2004 (menuUpdate @0x41b0b0). */
void menuRowDraw(const char *label, int y, int bSelected)
{
    gxFont      *small = bSelected ? g_hMenuMsfnt : g_hMenuFontSmall;
    gxFont      *big   = bSelected ? g_hMenuMfnt  : g_hMenuFont;
    char         tok[128];
    int          x;
    const char  *p;

    if (small == NULL || big == NULL) return;
    x = 0x226 - menuRowWidth(label);
    p = label;

    while (*p != '\0') {
        const char *q;
        int         n;

        q = p;
        while (*q != '\0' && menuIsSmallChar((unsigned char)*q)) q = q + 1;
        n = (int)(q - p);
        if (n > 0) {
            memcpy(tok, p, (size_t)n); tok[n] = '\0';
            textDraw(small, 0x2004, x, y, tok);
            x += textWidth(small, tok);
            p = q;
            continue;
        }
        q = p;
        while (*q != '\0' && !menuIsSmallChar((unsigned char)*q)) q = q + 1;
        n = (int)(q - p);
        if (n > 0) {
            memcpy(tok, p, (size_t)n); tok[n] = '\0';
            textDraw(big, 0x2004, x, y, tok);
            x += textWidth(big, tok);
            p = q;
        }
    }
}

/* Present one intro logo, logging the first time each logo is shown. */
void introPresent(int idx)
{
    static int nShown = -1;
    if (idx != nShown) {
        nShown = idx;
        appLog("[intro] logo %d/%d %s @%.0f ms", idx + 1, 6,
               g_kIntroTga[idx], g_introFade_2);
    }
    presentFrame(g_hIntroTex[idx]);
}

/* Clear to g_nClearColor (0) + flip — the gap between intro logos
 * (introUpdate @0x41ae50: gxClearScreen(1,g_nClearColor); gxFlip();). */
void introClear(void)
{
    gxClearScreen(1, 0);   /* g_nClearColor @0x45892c = 0 */
    gxFlip();
}

/* --- GX driver state (formerly gx.c) --- */

/* g_driver — active GxDriver struct (mirrors the maniac GxDriverApi table
 * @0x45eb40 filled by gxDLLInit in gxSoft.dll). No direct original address
 * as a static: it is the maniac-side extension of GxDriverApi @0x45eb40. */
GxDriver g_driver;

/* --- font helpers (formerly font.c) --- */

/* Per-glyph quad: 16-byte vertex {x<<8, y<<8, 0, r,g,b}. Top two vertices
 * (v0,v1) use g_textColor2, bottom two (v2,v3) use g_textColor. */
typedef struct {
    int  x;                    /* +0x00 fixed-point coord */
    int  y;                    /* +0x04 */
    int  pad;                  /* +0x08 zero */
    unsigned char r, g, b, a;  /* +0x0c color */
} GxVert;                      /* 0x10 bytes */

/* colorUv passed to gxDrawPolygon (0x1c bytes). Layout verified against the
 * driver texture contract and the gxDrawPolygon wrapper repack @0x433440. */
typedef struct {
    void          *pTexture;  /* +0x00 */
    void          *pParam5;   /* +0x04 */
    int            pad;       /* +0x08 */
    unsigned short U;         /* +0x0c */
    unsigned short V;         /* +0x0e */
    unsigned short gwU;       /* +0x10 gw<<8|U */
    unsigned short V2;        /* +0x12 */
    unsigned short gwU2;      /* +0x14 */
    unsigned short hV;        /* +0x16 h<<8|V */
    unsigned short U2;        /* +0x18 */
    unsigned short hV2;       /* +0x1a h<<8|V */
} GxColorUv;                  /* 0x1c bytes */

/* Draw one glyph and advance the cursor (LAB_0040968e in textDraw @0x409420). */
void textDrawGlyph(gxFont *font, unsigned int color, unsigned char ch,
                   int *xPos, int yPos)
{
    int          gw = font->pGlyphWidth[ch];
    int          h  = (short)font->wPad2;
    unsigned int U  = font->pUvx[ch];
    unsigned int V  = font->pUvy[ch];
    int          x  = *xPos;
    GxVert       v0, v1, v2, v3;
    GxColorUv    cuv;

    v0.x = x << 8;               v0.y = yPos << 8;
    v1.x = (x + gw) << 8;        v1.y = yPos << 8;
    v2.x = (x + gw) << 8;        v2.y = (yPos + h) << 8;
    v3.x = x << 8;               v3.y = (yPos + h) << 8;
    v0.pad = v1.pad = v2.pad = v3.pad = 0;
    v0.r = (unsigned char)(g_textColor2 >> 0x10);
    v0.g = (unsigned char)(g_textColor2 >> 8);
    v0.b = (unsigned char)g_textColor2;
    v1.r = v0.r; v1.g = v0.g; v1.b = v0.b;
    v2.r = (unsigned char)(g_textColor >> 0x10);
    v2.g = (unsigned char)(g_textColor >> 8);
    v2.b = (unsigned char)g_textColor;
    v3.r = v2.r; v3.g = v2.g; v3.b = v2.b;
    v0.a = v1.a = v2.a = v3.a = 0;

    cuv.pTexture = font->pTexture;
    cuv.pParam5  = font->pParam5;
    cuv.pad      = 0;
    cuv.U  = (unsigned short)U;
    cuv.V  = (unsigned short)V;
    cuv.gwU = (unsigned short)((gw << 8) | U);
    cuv.V2  = (unsigned short)V;
    cuv.gwU2 = (unsigned short)((gw << 8) | U);
    cuv.hV  = (unsigned short)((h << 8) | V);
    cuv.U2  = (unsigned short)U;
    cuv.hV2 = (unsigned short)((h << 8) | V);

    gxDrawPolygon(&v0, &v1, &v2, &v3, color, &cuv);

    *xPos = x + (int)(short)font->wGlobalSpace + gw;
}

/* textIntToStr — the itoa used by textDrawInt/textIntWidth: shift the
 * 32-byte buffer right by one each digit, prefix '-' for negatives. */
char *textIntToStr(int value, char buf[32])
{
    int i;
    int neg = value < 0;

    buf[0] = '\0';
    do {
        for (i = 0x1c; i >= 0; i--) buf[i + 1] = buf[i];
        i = value % 10;
        buf[0] = (char)(abs(i) + '0');
        value = (value - i) / 10;
    } while (value != 0);

    if (neg) {
        for (i = 0x1c; i >= 0; i--) buf[i + 1] = buf[i];
        buf[0] = '-';
    }
    return buf;
}

/* --- shutdown (formerly stubs.c) --- */

/* shutdownRenderer — original gxUnloadDriver @0x432880 path (see gx.c).
 * Custom name: named shutdown entry point used by the full WinMain. */
void shutdownRenderer(void)
{
    /* Implemented via gx.c (gxUnloadDriver @0x432880). */
    gxShutdown();
}
