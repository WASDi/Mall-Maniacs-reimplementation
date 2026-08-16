#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "util.h"

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

void *loadTga640x480(const char *path)
{
    /* Mirrors tgaLoad16 @0x415df0: 640x480 8-bit indexed (type 1) or
     * grayscale (type 3) TGA; pixel-data offset computed as
     *   18 + idlen + (colormap_length * colormap_depth / 8)
     * (for intro_addgames.tga: 18 + 18 + (256*24/8) = 804, 307200 bytes). */
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