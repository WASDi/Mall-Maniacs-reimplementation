#ifndef UTIL_H
#define UTIL_H

#include "compat_types.h"
#include <stdio.h>
#include <stdarg.h>

/* File helper cluster — reimplementation of the maniac file* wrappers.
 * The original functions thunk to the statically-linked MSVC CRT
 * (fileOpen @0x43e68a = fopen, fileRead @0x43e306 = fread, fileSeek
 * @0x43e5a0 = fseek, fileTell @0x43e41d = ftell, fileClose @0x43e289 =
 * fclose). */

FILE *fileOpenMode(LPCSTR path, int mode); /* @0x408cd0 mode 1 -> "wb" else "rb"; NULL on fail (64-bit: FILE*, not int) */
void fileCloseStream(FILE *fp);                /* @0x408d00 */
void fileReadN(FILE *fp, char *buf, unsigned int count); /* @0x408d10 */
void fileSeekTell(FILE *fp, int offset, int mode);       /* @0x408d30 mode 0/1/other -> SET/CUR/END */
char *fileReadRaw(int pool, LPCSTR path);  /* @0x408d60 pool-owned buffer or NULL */
char *fileReadText(int pool, LPCSTR path); /* @0x408e20 NUL-terminated pool buffer or NULL */
int  fileGetSizeOpen(FILE *fp);                 /* @0x408ee0 current offset preserved */
int  fileGetSize(LPCSTR path);                  /* @0x408f20 0 on failure */
int  fileExists(LPCSTR path);                   /* @0x408f60 */
int  fileDelete(LPCSTR path);                   /* @0x43e126 CRT remove glue */

/* fmtSprintf @0x43e767 — CRT sprintf glue used across the game. */
int  fmtSprintf(char *pBuf, const char *pFmt, ...);

/* fmtSscanf @0x43e69d — CRT sscanf glue (crtFscanfCore over the string). */
int  fmtSscanf(const char *pStr, const char *pFmt, ...);

/* fmtAtoi @0x43e75c — thin wrapper over fmtAtoiCore @0x43e6d1 (= CRT atoi:
 * skip whitespace, optional sign, decimal digits). The original is
 * __thiscall with an unused ECX operand (callers pass the source node
 * name there); only the string argument matters. */
int  fmtAtoi(const char *pszText);

/* fatalError @0x414570 — scene/sound teardown + message box + exit. */
void fatalError(const char *pFmt, ...);

/* Directory enumeration abstraction (Phase 5): platform-specific
 * implementations behind one contract. Returns 1 when the directory
 * could be opened. */
int utilScanDir(const char *dir, int (*cb)(const char *name, void *ctx), void *ctx);

#endif /* UTIL_H */
