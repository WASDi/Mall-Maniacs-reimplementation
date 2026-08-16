#ifndef GX_H
#define GX_H

#include <windows.h>

/* GX driver interface (gxSoft.dll). Layout matches the table filled by
 * gxDLLInit @0x10001000 in gxSoft.dll. Original struct: GxDriverApi @0x45eb40
 * in maniac.exe (136 bytes total, 0x7c driver-owned + 0x80 hModule + 0x84 active).
 * Offsets below verified against gxDLLInit's fills. */

typedef struct GxMode {
    unsigned short width;     /* +0x00 = 0x280 (640) */
    unsigned short height;    /* +0x02 = 0x1e0 (480) */
    unsigned char  bpp;       /* +0x04 = 16 (driver forces to 8) */
    unsigned char  pad[3];
    unsigned int   hInstance; /* +0x08 */
    unsigned int   hwnd;      /* +0x0c */
} GxMode;                     /* 16 bytes */

typedef struct GxDriverApi {
    void *pUnused00;                       /* +0x00 (zero) */
    int  (*pSetMode)(GxMode *mode);        /* +0x04 -> gxSetMode @0x10001130 */
    int  (*pGetMode)(GxMode *mode);        /* +0x08 -> gxGetMode @0x10001110 */
    void (*pSnooze)(void);                 /* +0x0c -> gxSnooze @0x100014e0 */
    void *pField10;                        /* +0x10 -> gxField10 @0x10001760 */
    void *pField14;                        /* +0x14 -> gxField14 @0x10001890 */
    int  (*pFlip)(void);                   /* +0x18 -> gxFlip @0x10001520 */
    void *pUnused1c;                       /* +0x1c (zero) */
    void (*pClearScreen)(int, int);        /* +0x20 -> gxClearScreen @0x10001730 */
    void *pUnused24;                       /* +0x24 */
    void *pUnused28;                       /* +0x28 */
    void *pUnused2c;                       /* +0x2c */
    void *pUnused30;                       /* +0x30 */
    void *pUnused34;                       /* +0x34 */
    void *pSetViewport;                    /* +0x38 -> gxSetViewport @0x100018f0 */
    void *pGetViewport;                    /* +0x3c -> gxGetViewport @0x10001950 */
    void (*pResetState)(void);             /* +0x40 -> gxResetState @0x10001980 */
    int  (*pLoadTexture)(int, int, const char *, void *, void *);
                                           /* +0x44 -> gxLoadTexture @0x100019b0 */
    void (*pUpdate)(void);                 /* +0x48 -> gxDLLUpdate @0x10001f20 */
    void *pCreateSurface;                  /* +0x4c -> gxCreateSurface @0x10001f30 */
    void *pData50;                         /* +0x50 -> &DAT_10002050 */
    void *pData54;                         /* +0x54 -> &DAT_10002050 */
    void *pData58;                         /* +0x58 -> &LAB_10002060 */
    void *pDrawPolygon;                    /* +0x5c -> gxDrawPolygon @0x100020e0 */
    void (*pUpdate2)(void);                /* +0x60 -> gxDLLUpdate */
    void (*pBlitSurface)(int, int, int, void *, int, int, int, int, int);
                                           /* +0x64 -> gxBlitSurface @0x10002950 */
    void *pSetOrigin;                      /* +0x68 -> gxSetOrigin @0x10001f90 */
    void *pData6c;                         /* +0x6c -> &DAT_10002050 */
    void *pData70;                         /* +0x70 -> &DAT_10002050 */
    void *pDrawTriUV;                      /* +0x74 -> gxDrawTriUV @0x100021f0 */
    void *pDrawQuad;                       /* +0x78 -> gxDrawQuad @0x10002540 */
} GxDriverApi;                             /* 0x7c bytes driver-owned */

/* maniac-side extension of GxDriverApi (struct @0x45eb40, 136 bytes):
 * +0x80 hDriverModule, +0x84 nDriverActive */
typedef struct GxDriver {
    GxDriverApi api;
    HMODULE     hDriverModule;   /* +0x80 */
    int         nDriverActive;   /* +0x84 */
} GxDriver;

/* Reimplementation of maniac gxInit @0x4332f0 (loads driver + pSetMode). */
int  gxInit(GxMode *mode);
/* Reimplementation of maniac gxUnloadDriver @0x432880 (narrowed: no registry). */
void gxShutdown(void);
/* Reimplementation of maniac presentFrame @0x410310. */
void presentFrame(void *texture);
/* Wrapper for gxLoadTexture(0,1,name,data,data+0x10000); data = .tpg file bytes. */
int  gxLoadTexture(const char *name, void *data);

#endif /* GX_H */