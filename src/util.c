#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "pool.h"
#include "util.h"

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

/* --- file helper cluster (fileOpenMode @0x408cd0 .. fileExists @0x408f60) --- */

/* fileOpenMode @0x408cd0 — fopen with "wb"/"rb"; FILE* as int, -1 on fail. */
int fileOpenMode(const char *path, int mode)
{
    FILE *f = fopen(path, (mode == 1) ? "wb" : "rb");   /* g_sz_wb @0x44e6c8 / g_sz_rb @0x44e6c0 */
    return (f != NULL) ? (int)(size_t)f : -1;
}

/* fileCloseStream @0x408d00 */
void fileCloseStream(FILE *fp)
{
    if (fp != NULL) fclose(fp);
}

/* fileReadN @0x408d10 — fread buf, count bytes. */
void fileReadN(FILE *fp, void *buf, unsigned int count)
{
    if (fp != NULL && buf != NULL)
        (void)fread(buf, 1, count, fp);
}

/* fileSeekTell @0x408d30 — mode 0/1/2 -> SEEK_SET/SEEK_CUR/SEEK_END. */
void fileSeekTell(FILE *fp, int offset, int mode)
{
    int whence;
    if (fp == NULL) return;
    whence = (mode == 1) ? SEEK_CUR : (mode == 2) ? SEEK_END : SEEK_SET;
    (void)fseek(fp, offset, whence);
    (void)ftell(fp);
}

/* fileReadRaw @0x408d60 — whole file into a pool-owned buffer (not NUL
 * terminated); NULL on any failure. */
char *fileReadRaw(int pool, const char *path)
{
    int size;
    char *buf;
    FILE *f;

    if (!fileExists(path)) return NULL;
    size = fileGetSize(path);
    if (size == 0) return NULL;
    buf = (char *)memPoolAlloc(pool, (size_t)size);
    if (buf == NULL) return NULL;

    f = fopen(path, "rb");
    if (f == NULL) return NULL;
    if (fread(buf, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        return NULL;
    }
    fclose(f);
    return buf;
}

/* fileReadText @0x408e20 — whole file + NUL terminator into a pool buffer. */
char *fileReadText(int pool, const char *path)
{
    int size;
    char *buf;
    FILE *f;

    if (!fileExists(path)) return NULL;
    size = fileGetSize(path);
    if (size == 0) return NULL;
    buf = (char *)memPoolAlloc(pool, (size_t)size + 1);
    if (buf == NULL) return NULL;

    f = fopen(path, "rb");
    if (f == NULL) return NULL;
    if (fread(buf, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        return NULL;
    }
    fclose(f);
    buf[size] = '\0';
    return buf;
}

/* fileGetSizeOpen @0x408ee0 — file size; restores the current offset. */
int fileGetSizeOpen(FILE *fp)
{
    int cur, end;
    if (fp == NULL) return 0;
    cur = (int)ftell(fp);
    (void)fseek(fp, 0, SEEK_END);
    end = (int)ftell(fp);
    (void)fseek(fp, cur, SEEK_SET);
    return end;
}

/* fileGetSize @0x408f20 — size by path, 0 on failure. */
int fileGetSize(const char *path)
{
    FILE *f;
    int size;
    f = fopen(path, "rb");      /* g_szCmdFont (CRT mode string) == "rb" */
    if (f == NULL) return 0;
    size = fileGetSizeOpen(f);
    fclose(f);
    return size;
}

/* fileExists @0x408f60 — 1 if the path can be opened for read. */
int fileExists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return 0;
    fclose(f);
    return 1;
}