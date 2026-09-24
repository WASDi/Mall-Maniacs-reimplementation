#include "compat_types.h"
#include <string.h>
#include <stdint.h>
#include "pool.h"
#include "util.h"

/* =====================================================================
 * TGA loader — faithfully mirrors assembly of tgaLoad16 @0x415df0,
 * tgaLoad16Pal @0x415ec0 and dispatcher imageLoadByMode @0x4102e0.
 * Variable names follow header fields, not decompiler temporaries.
 * ===================================================================== */

extern int g_nGfxMode; /* @0x4580c4 — 1=Glide, 2=Software */

/* tgaLoad16 @0x415df0 — 16-bit TGA (no palette). Header at pFileData:
 *   +0  idLen, +5  width (u16), +7  colorDepth, +0x11 descriptor.
 *   Data offset = idLen + 0x12 + ((colorDepth*width) aligned >>3).
 *   If descriptor 0x20 clear, image is bottom-up and needs row flip
 *   via the 0x4ad80 stride; else straight copy. Returns 0x4b000 pool buffer. */
unsigned short *tgaLoad16(LPCSTR pszFilename) /* @0x415df0 */
{
    char *pFileData;
    unsigned short *pDestPixels;
    int pixelDataOffset;
    int headerIdLen;
    int imageWidth;
    int colorDepth;
    int alignedBits;

    pFileData = fileReadRaw(0, pszFilename);
    if (pFileData == NULL) {
        return NULL;
    }

    /* pixelDataOffset = idLen + 0x12 + ((colorDepth*width) rounding up) >>3
     * Assembly: MOVSX EAX, [EBP+7]; IMUL EAX, [EBP+5]; CDQ; AND EDX,7; ADD; SAR 3 */
    headerIdLen = (unsigned char)pFileData[0];
    imageWidth  = *(unsigned short *)(pFileData + 5);
    colorDepth  = (unsigned char)pFileData[7];
    alignedBits = colorDepth * imageWidth;
    pixelDataOffset = headerIdLen + 0x12 + ((alignedBits + ((alignedBits >> 31) & 7)) >> 3);

    pDestPixels = (unsigned short *)memPoolAlloc(0, 0x4b000);
    /* Descriptor bit 0x20 = top-to-bottom; 0 = bottom-up (flip). */
    if ((pFileData[0x11] & 0x20) == 0) {
        int rowOffset = 0;
        /* Flip path: 0x4b000 bytes = 480 rows * 0x280 stride (640). */
        do {
            int col = 0;
            int nextCol;
            unsigned short *pDestRow = (unsigned short *)((uintptr_t)pDestPixels + (uintptr_t)rowOffset);
            do {
                nextCol = col + 2;
                /* Source = pFileData + pixelDataOffset + (col - rowOffset) + 0x4ad80 */
                *pDestRow = *(unsigned short *)(pFileData + (col - rowOffset) + 0x4ad80 + pixelDataOffset);
                col = nextCol;
                pDestRow++;
            } while (nextCol < 0x280);
            rowOffset += 0x280;
        } while (rowOffset < 0x4b000);
        memPoolFree(0, pFileData);
        return pDestPixels;
    }

    /* Top-down straight copy: 0x25800 words = 0x4b000 bytes. */
    {
        int remaining = 0x25800;
        unsigned short *pDst = pDestPixels;
        /* EDI = pFileData + pixelDataOffset, EAX = pDestPixels */
        intptr_t baseDelta = (intptr_t)pixelDataOffset - (intptr_t)pDestPixels;
        do {
            *pDst = *(unsigned short *)(pFileData + baseDelta + (intptr_t)pDst);
            pDst++;
            remaining--;
        } while (remaining != 0);
    }
    memPoolFree(0, pFileData);
    return pDestPixels;
}

/* tgaLoad16Pal @0x415ec0 — paletted TGA with 768-byte palette expansion.
 * Reorders BGR triples into paletteTemp[768] then converts indexed
 * 0x4b000 pixels through palette to 0x96000 RGB555 buffer. */
short *tgaLoad16Pal(LPCSTR pszFilename) /* @0x415ec0 */
{
    char *pFileData;
    unsigned short *pIndexedPixels;
    short *pFinalPixels;
    unsigned char paletteTemp[768];
    unsigned char *pColorMap;
    int pixelDataOffset;
    int headerIdLen;
    int imageWidth;
    int colorDepth;
    int alignedBits;

    pFileData = fileReadRaw(0, pszFilename);
    if (pFileData == NULL) {
        return NULL;
    }

    headerIdLen = (unsigned char)pFileData[0];
    imageWidth  = *(unsigned short *)(pFileData + 5);
    colorDepth  = (unsigned char)pFileData[7];
    alignedBits = colorDepth * imageWidth;
    pixelDataOffset = headerIdLen + 0x12 + ((alignedBits + ((alignedBits >> 31) & 7)) >> 3);

    pIndexedPixels = (unsigned short *)memPoolAlloc(0, 0x4b000);
    pFinalPixels   = (short *)memPoolAlloc(0, 0x96000);
    pColorMap = (unsigned char *)(pFileData + headerIdLen + 0x12);

    /* Palette depth 0x18 (24-bit) or 0x20 (32-bit with alpha). */
    if (colorDepth == 0x18) {
        unsigned int entryCount = (unsigned int)imageWidth;
        if (entryCount != 0) {
            unsigned char *pDst = paletteTemp + 1;
            do {
                pDst[1] = pColorMap[0];
                pDst[0] = pColorMap[1];
                pDst[-1] = pColorMap[2];
                pColorMap += 3;
                entryCount--;
                pDst += 3;
            } while (entryCount != 0);
        }
    } else if (colorDepth == 0x20) {
        unsigned int entryCount = (unsigned int)imageWidth;
        if (entryCount != 0) {
            unsigned char *pDst = paletteTemp + 1;
            do {
                pDst[1] = pColorMap[0];
                pDst[0] = pColorMap[1];
                pDst[-1] = pColorMap[2];
                pColorMap += 4;
                entryCount--;
                pDst += 3;
            } while (entryCount != 0);
        }
    }

    if ((pFileData[0x11] & 0x20) == 0) {
        int rowOffset = 0;
        do {
            int col = 0;
            int nextCol;
            unsigned short *pDestRow = (unsigned short *)((uintptr_t)pIndexedPixels + (uintptr_t)rowOffset);
            do {
                nextCol = col + 2;
                *pDestRow = *(unsigned short *)(pFileData + (col - rowOffset) + 0x4ad80 + pixelDataOffset);
                col = nextCol;
                pDestRow++;
            } while (nextCol < 0x280);
            rowOffset += 0x280;
        } while (rowOffset < 0x4b000);
    } else {
        int remaining = 0x25800;
        unsigned short *pDst = pIndexedPixels;
        intptr_t baseDelta = (intptr_t)pixelDataOffset - (intptr_t)pIndexedPixels;
        do {
            *pDst = *(unsigned short *)(pFileData + baseDelta + (intptr_t)pDst);
            pDst++;
            remaining--;
        } while (remaining != 0);
    }

    /* Palette-indexed to RGB555: 0x4b000 indices -> 0x96000 bytes. */
    {
        int idx = 0;
        short *pOut = pFinalPixels;
        do {
            unsigned int palIndex = (unsigned int)*((unsigned char *)pIndexedPixels + idx) * 3;
            idx++;
            /*  pal[+2]>>3 | (pal[0]&0xf8)<<5 | (pal[1]&0x1ffc)<<3  — assembly 1600e-16031 */
            *pOut = (short)(paletteTemp[palIndex + 2] >> 3) +
                    (short)(((paletteTemp[palIndex] & 0xf8) * 0x20 + (paletteTemp[palIndex + 1] & 0x1ffc)) * 8);
            pOut++;
        } while (idx < 0x4b000);
    }

    memPoolFree(0, pFileData);
    memPoolFree(0, pIndexedPixels);
    return pFinalPixels;
}
