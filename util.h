#ifndef UTIL_H
#define UTIL_H

#include <windows.h>

/* Startup / asset / error logging. Appends a line to "rebuild.log" in the
 * working directory (the game-data dir per Rebuild.md) so visual testing and
 * the 5s timeout are not the only diagnostics. */
void appLog(const char *fmt, ...);

/* Read an entire file into a malloc'd buffer. Returns NULL on failure;
 * *outSize receives the byte count. No direct original address (replacement
 * helper for the fileReadRaw family used by menuInit @0x419c20). */
void *readFileAlloc(const char *path, size_t *outSize);

/* Load an 8-bit indexed 640x480 TGA (type 1 / type 3 / uncompressed) into a
 * malloc'd 0x4b000 byte index buffer, matching tgaLoad16 @0x415df0's pixel
 * extraction (same palette buffer is NOT stored — the DD palette comes from
 * the first .tpg load, matching menuInit's ordering). Returns NULL on failure. */
void *loadTga640x480(const char *path);

#endif /* UTIL_H */