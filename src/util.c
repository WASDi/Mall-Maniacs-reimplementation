#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#ifndef _WIN32
#include <dirent.h>
#else
#include <io.h>
#endif
#include "pool.h"
#include "util.h"
#include "gx.h"
#include "sen.h"
#include "scene.h"
#include "custom_helpers.h"

#include "platform_sdl2.h"

extern void *g_hWnd;   /* @0x459ce0 (defined in platform_sdl2.c) */

/* --- file helper cluster (fileOpenMode @0x408cd0 .. fileExists @0x408f60) --- */

/* Canonical resolver (Phase 5): separator normalization + data-directory
 * resolution, then the Windows-parity case-insensitive fallback
 * (platformFopenCI) for read-only assets — config/.sen literals don't
 * always match disk casing. Exact case is always tried first; literals
 * that already match never touch the fallback. Write modes ("wb")
 * resolve against the user pref dir, never beside installed assets. */
static FILE *fopen_normalized(const char *path, const char *mode)
{
    char norm[1024], full[2048];
    size_t n;
    int isWrite = (mode[0] == 'w' || mode[0] == 'a');
    FILE *f;
    if (!path) return NULL;
    n = strlen(path);
    if (n >= sizeof(norm)) n = sizeof(norm) - 1;
    for (size_t i = 0; i < n; i++) norm[i] = (path[i] == '\\') ? '/' : path[i];
    norm[n] = '\0';
    if (isWrite) {
        const char *pref = platformPrefDir();
        if (pref && pref[0]) {
            const char *base = strrchr(norm, '/');
            base = base ? base + 1 : norm;
            snprintf(full, sizeof(full), "%s/%s", pref, base);
            f = fopen(full, mode);
            if (f) return f;
        }
        return fopen(norm, mode);
    }
    f = fopen(norm, mode);
    if (f) return f;
    /* Data-dir lookup for read-only assets. */
    platformAssetPath(norm, full, sizeof(full));
    f = fopen(full, mode);
    if (f) return f;
    /* Last resort: component-wise case-insensitive match. */
    f = platformFopenCI(norm, mode);
    if (f) return f;
    return platformFopenCI(full, mode);
}

/* Directory enumeration abstraction (Phase 5): platform-specific
 * implementations behind one contract. Linux/macOS use dirent; Windows
 * uses the CRT findfirst set (no Win32 GUI headers needed). Callback
 * receives each entry name; nonzero return stops the scan. */
int utilScanDir(const char *dir, int (*cb)(const char *name, void *ctx), void *ctx)
{
    char norm[1024], full[2048];
    size_t n;
    if (!dir || !cb) return 0;
    n = strlen(dir);
    if (n >= sizeof(norm)) n = sizeof(norm) - 1;
    for (size_t i = 0; i < n; i++) norm[i] = (dir[i] == '\\') ? '/' : dir[i];
    norm[n] = '\0';
    platformAssetPath(norm, full, sizeof(full));
#ifdef _WIN32
    {
        /* NOTE: native Windows build only; Linux uses dirent below. */
        char search[2200];
        struct _finddata_t fd;
        intptr_t h;
        snprintf(search, sizeof(search), "%s/*", full);
        h = _findfirst(search, &fd);
        if (h == -1) return 0;
        do {
            if (cb(fd.name, ctx)) break;
        } while (_findnext(h, &fd) == 0);
        _findclose(h);
        return 1;
    }
#else
    {
        DIR *d = opendir(full);
        struct dirent *e;
        if (!d) {
            d = opendir(norm);
            if (!d) return 0;
        }
        while ((e = readdir(d)) != NULL) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            if (cb(e->d_name, ctx)) break;
        }
        closedir(d);
        return 1;
    }
#endif
}

/* fileOpenMode @0x408cd0 — fopen with "wb"/"rb".
 * 64-bit port: returns FILE* directly (original returned the pointer as an
 * int/0xffffffff sentinel, which truncates on 64-bit). Callers test NULL. */
FILE *fileOpenMode(LPCSTR path, int mode)
{
    return fopen_normalized(path, (mode == 1) ? "wb" : "rb");   /* g_sz_wb @0x44e6c8 / g_sz_rb @0x44e6c0 */
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

    f = fopen_normalized(path, "rb");
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

    f = fopen_normalized(path, "rb");
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
    f = fopen_normalized(path, "rb");      /* g_szCmdFont (CRT mode string) == "rb" */
    if (f == NULL) return 0;
    size = fileGetSizeOpen(f);
    fclose(f);
    return size;
}

/* fileExists @0x408f60 — 1 if the path can be opened for read. */
int fileExists(LPCSTR path)
{
    FILE *f = fopen_normalized(path, "rb");
    if (f == NULL) return 0;
    fclose(f);
    return 1;
}

/* fileDelete @0x43e126 — CRT remove() glue. */
int fileDelete(LPCSTR path) /* @0x43e126 */
{
    return remove(path);
}

/* fmtSprintf @0x43e767 — CRT sprintf glue (crtVfprintfCore into a memory
 * stream, unbounded). */
int fmtSprintf(char *pBuf, const char *pFmt, ...) /* @0x43e767 */
{
    int n;
    va_list args;
    va_start(args, pFmt);
    n = vsprintf(pBuf, pFmt, args);
    va_end(args);
    return n;
}

/* fmtSscanf @0x43e69d — CRT sscanf glue (crtFscanfCore drives the scanf
 * core over the source string; vararg out parameters). */
int fmtSscanf(const char *pStr, const char *pFmt, ...) /* @0x43e69d */
{
    int n;
    va_list args;
    va_start(args, pFmt);
    n = vsscanf(pStr, pFmt, args);
    va_end(args);
    return n;
}

/* fatalError @0x414570 — tear the render/scene systems down, show the
 * message box and quit. The original calls exitProc @0x43ed1c (CRT exit
 * glue: atexit handlers + ExitProcess); the rebuild maps it to exit(). */
void fatalError(const char *pFmt, ...) /* @0x414570 */
{
    char buf[256];
    va_list args;

    scenNameTableFree();
    sceneSystemClose();
    gxUnloadDriver();
    va_start(args, pFmt);
    vsnprintf(buf, sizeof(buf), pFmt, args);
    va_end(args);
    appLog("[fatalError] %s", buf);     /* log before the modal box so headless runs show the cause */
    platformShowError("Mall Maniacs - Error", buf);  /* caption @0x44ffe4; SDL msgbox + stderr */
    exit(-1);                             /* exitProc(0xffffffff) */
}
/* ===================================================================
 * fmtAtoi @0x43e75c / fmtAtoiCore @0x43e6d1
 * =================================================================== */

/* fmtAtoi @0x43e75c — CRT atoi (fmtAtoiCore): skip whitespace, optional
 * +/-, parse decimal digits. The ECX 'this' operand of the original
 * thiscall is never dereferenced by fmtAtoiCore, so the rebuild exposes
 * only the string argument. 64-bit-port hardening: commandDispatch
 * returns NULL for unhandled "get ..." queries (stubs.c contract), so a
 * NULL reply means "no data" (0) instead of crashing in atoi. */
int fmtAtoi(const char *pszText) /* @0x43e75c */
{
    if (pszText == NULL) return 0;
    return atoi(pszText);   /* fmtAtoiCore @0x43e6d1 */
}
