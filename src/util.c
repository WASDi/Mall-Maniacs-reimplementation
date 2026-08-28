#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "pool.h"
#include "util.h"
#include "gx.h"
#include "sen.h"
#include "scene.h"
#include "custom_helpers.h"

extern HWND g_hWnd;   /* maniac g_hMainWindow @0x459ce0 (defined in maniac.c) */

/* --- file helper cluster (fileOpenMode @0x408cd0 .. fileExists @0x408f60) --- */

/* path normalize helper — convert Windows separators to POSIX and try
 * case variants. The original Windows CRT is case-insensitive; the rebuild
 * runs under Wine/linux where fopen is case-sensitive and '\\' is literal.
 * Keep original disassembly behavior (direct fopen) first, then fall back. */
static FILE *fopen_normalized(const char *path, const char *mode)
{
    FILE *f = fopen(path, mode);
    if (f) return f;
    char alt[1024];
    size_t n = strlen(path);
    if (n >= sizeof(alt)) n = sizeof(alt)-1;
    for (size_t i=0;i<n;i++) alt[i] = (path[i]=='\\') ? '/' : path[i];
    alt[n]='\0';
    f = fopen(alt, mode);
    if (f) return f;
    /* try lowercasing (CHARACTERS.SEN vs characters.sen) */
    for (size_t i=0;i<n;i++) alt[i] = (char)tolower((unsigned char)alt[i]);
    f = fopen(alt, mode);
    return f;
}

/* fileOpenMode @0x408cd0 — fopen with "wb"/"rb"; FILE* as int, -1 on fail. */
int fileOpenMode(LPCSTR path, int mode)
{
    FILE *f = fopen_normalized(path, (mode == 1) ? "wb" : "rb");   /* g_sz_wb @0x44e6c8 / g_sz_rb @0x44e6c0 */
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
    MessageBoxA(g_hWnd, buf, "Mall Maniacs - Error", MB_ICONERROR);  /* caption @0x44ffe4, uType 0x10 */
    exit(-1);                             /* exitProc(0xffffffff) */
}
/* ===================================================================
 * fmtAtoi @0x43e75c / fmtAtoiCore @0x43e6d1
 * =================================================================== */

/* fmtAtoi @0x43e75c — CRT atoi (fmtAtoiCore): skip whitespace, optional
 * +/-, parse decimal digits. The ECX 'this' operand of the original
 * thiscall is never dereferenced by fmtAtoiCore, so the rebuild exposes
 * only the string argument. */
int fmtAtoi(const char *pszText) /* @0x43e75c */
{
    return atoi(pszText);   /* fmtAtoiCore @0x43e6d1 */
}
