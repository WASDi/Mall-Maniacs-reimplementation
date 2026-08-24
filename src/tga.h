#ifndef TGA_H
#define TGA_H
#include <windows.h>
unsigned short *tgaLoad16(LPCSTR pszFilename); /* @0x415df0 */
short *tgaLoad16Pal(LPCSTR pszFilename);      /* @0x415ec0 */
#endif
