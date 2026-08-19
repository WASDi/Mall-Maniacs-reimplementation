/* render_font.c — offline visualizer for the maniac font system.
 *
 * Parses a game font descriptor (.txt, see src/font.c fontParse) through the
 * real fontLoad/fontParse code, then decodes the paired .tpg texture
 * (256*256 8-bit indices + 256 RGBA palette, docs/10-fileformats.md) directly
 * and blits every character 0..255 into a 16x16 grid BMP so the glyphs can be
 * inspected without running the game.
 *
 * Glyph source rect per char comes straight from fontParse's output:
 *   U = font->pUvx[c] >> 8, V = font->pUvy[c] >> 8 (posX/posY = 0)
 *   w = font->pGlyphWidth[c], h = font->wPad2
 *
 * Usage:
 *   render_font [descriptor.txt] [texture.tpg] [output.bmp]
 * Defaults: menu/tinyfont.txt, menu/TINY00.TPG in the shipped game dir, and
 * font_all_chars.bmp in the tests/ directory.
 *
 * Build (from repo root):
 *   i686-w64-mingw32-gcc -m32 -O2 -I src \
 *       tests/render_font.c src/font.c src/pool.c src/util.c -o /tmp/render_font
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "font.h"
#include "gx.h"
#include "pool.h"
#include "util.h"

/* textDraw's external (gxDrawPolygon) is never reached here; provide a stub so
 * font.c links without pulling in the GXSOFT driver stack. */
void gxDrawPolygon(GxVert *v0, GxVert *v1, GxVert *v2, GxVert *v3, int flags,
                   GxColorUv *colorUv)
{
    (void)v0; (void)v1; (void)v2; (void)v3; (void)flags; (void)colorUv;
}

#define TPG_INDEX_BYTES  0x10000   /* 256*256 8-bit palette indices */
#define TPG_PALETTE_BYTES 0x400    /* 256 RGBA entries */
#define TPG_FILE_BYTES   (TPG_INDEX_BYTES + TPG_PALETTE_BYTES)

typedef struct {
    unsigned char index[TPG_INDEX_BYTES];
    unsigned char palette[256][4]; /* RGBA */
} Tpg;

/* Read the raw .tpg straight off disk (format verified in docs/10-fileformats.md).
 * Returns 1 on success. */
static int tpgLoad(const char *path, Tpg *tpg)
{
    FILE *fp = fopen(path, "rb");
    size_t got;
    if (fp == NULL) return 0;
    got = fread(tpg->index, 1, TPG_INDEX_BYTES, fp);
    if (got != TPG_INDEX_BYTES) { fclose(fp); return 0; }
    got = fread(tpg->palette, 1, TPG_PALETTE_BYTES, fp);
    if (got != TPG_PALETTE_BYTES) { fclose(fp); return 0; }
    fclose(fp);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Minimal 4x6 pixel font for the hex code labels under each glyph.    */
/* ------------------------------------------------------------------ */

static const unsigned char g_hexFont[16][6] = {
    { 0x0e, 0x09, 0x09, 0x09, 0x09, 0x0e }, /* 0 */
    { 0x04, 0x0c, 0x04, 0x04, 0x04, 0x0e }, /* 1 */
    { 0x0e, 0x01, 0x01, 0x0e, 0x08, 0x0f }, /* 2 */
    { 0x0e, 0x01, 0x06, 0x01, 0x01, 0x0e }, /* 3 */
    { 0x09, 0x09, 0x0f, 0x01, 0x01, 0x01 }, /* 4 */
    { 0x0f, 0x08, 0x0e, 0x01, 0x01, 0x0e }, /* 5 */
    { 0x06, 0x08, 0x0e, 0x09, 0x09, 0x06 }, /* 6 */
    { 0x0f, 0x01, 0x02, 0x04, 0x04, 0x04 }, /* 7 */
    { 0x06, 0x09, 0x06, 0x09, 0x09, 0x06 }, /* 8 */
    { 0x06, 0x09, 0x07, 0x01, 0x01, 0x06 }, /* 9 */
    { 0x06, 0x09, 0x09, 0x0f, 0x09, 0x09 }, /* A */
    { 0x0e, 0x09, 0x0e, 0x09, 0x09, 0x0e }, /* B */
    { 0x07, 0x08, 0x08, 0x08, 0x08, 0x07 }, /* C */
    { 0x0e, 0x09, 0x09, 0x09, 0x09, 0x0e }, /* D */
    { 0x0f, 0x08, 0x0e, 0x08, 0x08, 0x0f }, /* E */
    { 0x0f, 0x08, 0x0e, 0x08, 0x08, 0x08 }, /* F */
};

static void blitLabel(unsigned char *rgb, int width, int height,
                      int x, int y, unsigned char value, int labelColor)
{
    const unsigned char hi = (value >> 4) & 0xf;
    const unsigned char lo = value & 0xf;
    int row, col;

    for (row = 0; row < 6; row++) {
        for (col = 0; col < 4; col++) {
            unsigned char set;
            int px, py;
            if ((g_hexFont[hi][row] >> (3 - col)) & 1) {
                px = x + col; py = y + row;
                if (px >= 0 && px < width && py >= 0 && py < height) {
                    rgb[(py * width + px) * 3 + 0] = (unsigned char)(labelColor >> 16);
                    rgb[(py * width + px) * 3 + 1] = (unsigned char)(labelColor >> 8);
                    rgb[(py * width + px) * 3 + 2] = (unsigned char)labelColor;
                }
            }
            set = (g_hexFont[lo][row] >> (3 - col)) & 1;
            if (set) {
                px = x + 5 + col; py = y + row;
                if (px >= 0 && px < width && py >= 0 && py < height) {
                    rgb[(py * width + px) * 3 + 0] = (unsigned char)(labelColor >> 16);
                    rgb[(py * width + px) * 3 + 1] = (unsigned char)(labelColor >> 8);
                    rgb[(py * width + px) * 3 + 2] = (unsigned char)labelColor;
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* BMP writer: 24-bit bottom-up. Takes an RGB buffer and emits the     */
/* file's BGR byte order.                                              */
/* ------------------------------------------------------------------ */

static int bmpWrite(const char *path, const unsigned char *rgb, int width,
                    int height)
{
    int rowBytes = ((width * 3) + 3) & ~3;
    int dataSize = rowBytes * height;
    unsigned char hdr[54];
    unsigned char *line;
    FILE *fp;
    int y, x;

    memset(hdr, 0, sizeof hdr);
    hdr[0] = 'B'; hdr[1] = 'M';
    *(unsigned int *)(hdr + 2) = (unsigned int)(54 + dataSize);
    *(unsigned int *)(hdr + 10) = 54;
    *(unsigned int *)(hdr + 14) = 40;
    *(int *)(hdr + 18) = width;
    *(int *)(hdr + 22) = height;
    *(unsigned short *)(hdr + 26) = 1;
    *(unsigned short *)(hdr + 28) = 24;
    *(unsigned int *)(hdr + 34) = (unsigned int)dataSize;

    line = (unsigned char *)malloc((size_t)rowBytes);
    if (line == NULL) return 0;

    fp = fopen(path, "wb");
    if (fp == NULL) { free(line); return 0; }
    if (fwrite(hdr, 1, sizeof hdr, fp) != sizeof hdr) {
        fclose(fp); free(line); return 0;
    }
    for (y = height - 1; y >= 0; y--) {
        const unsigned char *src = rgb + y * width * 3;
        for (x = 0; x < width; x++) {
            line[x * 3 + 0] = src[x * 3 + 2]; /* B */
            line[x * 3 + 1] = src[x * 3 + 1]; /* G */
            line[x * 3 + 2] = src[x * 3 + 0]; /* R */
        }
        memset(line + width * 3, 0, (size_t)(rowBytes - width * 3));
        if (fwrite(line, 1, (size_t)rowBytes, fp) != (size_t)rowBytes) {
            fclose(fp); free(line); return 0;
        }
    }
    fclose(fp);
    free(line);
    return 1;
}

#define DEFAULT_DATA_DIR "/home/wasd/MallManiacsUnmodified/menu"

int main(int argc, char **argv)
{
    const char *descPath = DEFAULT_DATA_DIR "/tinyfont.txt";
    const char *tpgPath = DEFAULT_DATA_DIR "/TINY00.TPG";
    const char *outPath = "font_all_chars.bmp";
    const int labelColor = 0x7f7f7f;   /* grey hex labels */
    const int cellColor = 0x181828;    /* dark cell background */
    gxFont *font;
    Tpg tpg;
    int c, maxw = 1, h, cellW, cellH, width, height;
    int row;
    unsigned char *rgb;
    int ok;

    if (argc > 1) descPath = argv[1];
    if (argc > 2) tpgPath = argv[2];
    if (argc > 3) outPath = argv[3];

    memPoolSystemInit();
    fontPoolCreate();

    font = fontLoad((char *)descPath, NULL, 0, 0, NULL);
    if (font == NULL) {
        fprintf(stderr, "render_font: cannot load descriptor '%s'\n", descPath);
        return 1;
    }
    if (!tpgLoad(tpgPath, &tpg)) {
        fprintf(stderr, "render_font: cannot load texture '%s'\n", tpgPath);
        return 1;
    }

    h = (int)(short)font->wPad2;
    for (c = 0; c < 256; c++) {
        if (font->pGlyphWidth[c] > maxw) maxw = font->pGlyphWidth[c];
    }

    /* 16x16 grid of cells; each cell = glyph + hex code label underneath. */
    cellW = maxw + 4; if (cellW < 20) cellW = 20;
    cellH = h + 8;
    width = 16 * cellW;
    height = 16 * cellH;

    rgb = (unsigned char *)malloc((size_t)width * (size_t)height * 3);
    if (rgb == NULL) return 1;

    for (row = 0; row < height; row++) {
        unsigned char *line = rgb + (size_t)row * width * 3;
        int x;
        for (x = 0; x < width; x++) {
            line[x * 3 + 0] = (unsigned char)(cellColor >> 16);
            line[x * 3 + 1] = (unsigned char)(cellColor >> 8);
            line[x * 3 + 2] = (unsigned char)cellColor;
        }
    }

    for (c = 0; c < 256; c++) {
        int ox = (c % 16) * cellW;
        int oy = (c / 16) * cellH;
        int U = font->pUvx[c] >> 8;
        int V = font->pUvy[c] >> 8;
        int gw = font->pGlyphWidth[c];
        int x, y;

        for (y = 0; y < h; y++) {
            for (x = 0; x < gw; x++) {
                int sx = U + x;
                int sy = V + y;
                int px = ox + x;
                int py = oy + y;
                if (sx < 0 || sx >= 256 || sy < 0 || sy >= 256) continue;
                if (px >= width || py >= height) continue;
                rgb[(py * width + px) * 3 + 0] = tpg.palette[tpg.index[sy * 256 + sx]][0];
                rgb[(py * width + px) * 3 + 1] = tpg.palette[tpg.index[sy * 256 + sx]][1];
                rgb[(py * width + px) * 3 + 2] = tpg.palette[tpg.index[sy * 256 + sx]][2];
            }
        }
        blitLabel(rgb, width, height, ox + (cellW - 9) / 2, oy + h + 1,
                  (unsigned char)c, labelColor);
    }

    ok = bmpWrite(outPath, rgb, width, height);
    free(rgb);
    if (!ok) {
        fprintf(stderr, "render_font: cannot write '%s'\n", outPath);
        return 1;
    }

    printf("render_font: wrote %s (%dx%d) from %s + %s\n",
           outPath, width, height, descPath, tpgPath);
    return 0;
}