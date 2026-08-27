#ifndef UTIL_H
#define UTIL_H

#include <windows.h>
#include <stdio.h>

/* File helper cluster — reimplementation of the maniac file* wrappers.
 * The original functions thunk to the statically-linked MSVC CRT
 * (fileOpen @0x43e68a = fopen, fileRead @0x43e306 = fread, fileSeek
 * @0x43e5a0 = fseek, fileTell @0x43e41d = ftell, fileClose @0x43e289 =
 * fclose). */

int  fileOpenMode(LPCSTR path, int mode); /* @0x408cd0 mode 1 -> "wb" else "rb"; -1 on fail */
void fileCloseStream(FILE *fp);                /* @0x408d00 */
void fileReadN(FILE *fp, char *buf, unsigned int count); /* @0x408d10 */
void fileSeekTell(FILE *fp, int offset, int mode);       /* @0x408d30 mode 0/1/other -> SET/CUR/END */
char *fileReadRaw(int pool, LPCSTR path);  /* @0x408d60 pool-owned buffer or NULL */
char *fileReadText(int pool, LPCSTR path); /* @0x408e20 NUL-terminated pool buffer or NULL */
int  fileGetSizeOpen(FILE *fp);                 /* @0x408ee0 current offset preserved */
int  fileGetSize(LPCSTR path);                  /* @0x408f20 0 on failure */
int  fileExists(LPCSTR path);                   /* @0x408f60 */

#endif /* UTIL_H */
