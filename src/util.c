#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pool.h"
#include "util.h"

/* --- file helper cluster (fileOpenMode @0x408cd0 .. fileExists @0x408f60) --- */

/* fileOpenMode @0x408cd0 — fopen with "wb"/"rb"; FILE* as int, -1 on fail. */
int fileOpenMode(LPCSTR path, int mode)
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
void fileReadN(FILE *fp, char *buf, unsigned int count)
{
    if (fp != NULL && buf != NULL)
        (void)fread(buf, 1, count, fp);
}

/* fileSeekTell @0x408d30 — mode 0 -> SEEK_SET, 1 -> SEEK_CUR, otherwise
 * SEEK_END, then discard the resulting tell position. */
void fileSeekTell(FILE *fp, int offset, int mode)
{
    int whence;
    if (fp == NULL) return;
    whence = (mode == 0) ? SEEK_SET : (mode == 1) ? SEEK_CUR : SEEK_END;
    (void)fseek(fp, offset, whence);
    (void)ftell(fp);
}

/* fileReadRaw @0x408d60 — whole file into a pool-owned buffer (not NUL
 * terminated); NULL on any failure. */
char *fileReadRaw(int pool, LPCSTR path)
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
char *fileReadText(int pool, LPCSTR path)
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
int fileGetSize(LPCSTR path)
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
int fileExists(LPCSTR path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return 0;
    fclose(f);
    return 1;
}