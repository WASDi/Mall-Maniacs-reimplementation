#ifndef GX_H
#define GX_H

#include <windows.h>

/* GX driver interface (gxSoft.dll). Layout matches the table filled by
 * gxDLLInit @0x10001000 in gxSoft.dll. Original struct: GxDriverApi @0x45eb40
 * in maniac.exe (136 bytes total, 0x80 driver-owned + 0x80 hModule + 0x84 active).
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
    void *pField_0;                        /* +0x00 (zero) */
    int  (*pSetMode)(GxMode *mode);        /* +0x04 -> gxSetMode @0x10001130 */
    int  (*pGetMode)(GxMode *mode);        /* +0x08 -> gxGetMode @0x10001110 */
    int  (*pSnooze)(void);                 /* +0x0c -> gxSnooze @0x100014e0 */
    void *pField_10;                       /* +0x10 -> gxField10 @0x10001760 */
    void *pField_14;                       /* +0x14 -> gxField14 @0x10001890 */
    int  (*pFlip)(void);                   /* +0x18 -> gxFlip @0x10001520 */
    void *pField_1c;                       /* +0x1c (zero) */
    int  (*pClearScreen)(int, int);        /* +0x20 -> gxClearScreen @0x10001730 */
    void *pField_24;                       /* +0x24 */
    void *pField_28;                       /* +0x28 */
    void *pField_2c;                       /* +0x2c */
    void *pField_30;                       /* +0x30 */
    void *pField_34;                       /* +0x34 */
    int  (*pSetViewport)(void *rect);      /* +0x38 -> gxSetViewport @0x100018f0 */
    int  (*pGetViewport)(void *rect);      /* +0x3c -> gxGetViewport @0x10001950 */
    int  (*pResetState)(void);             /* +0x40 -> gxResetState @0x10001980 */
    int  (*pLoadTexture)(int, int, const char *, void *, void *);
                                           /* +0x44 -> gxLoadTexture @0x100019b0 */
    void (*pUpdate)(void);                 /* +0x48 -> gxDLLUpdate @0x10001f20 */
    int  (*pCreateSurface)(const char *path); /* +0x4c -> gxCreateSurface @0x10001f30 */
    void *pData50;                         /* +0x50 -> &DAT_10002050 */
    void *pData54;                         /* +0x54 -> &DAT_10002050 */
    void *pData58;                         /* +0x58 -> &LAB_10002060 */
    void (*pDrawPolygon)(void *, void *, void *, void *, int, void *);
                                           /* +0x5c -> gxDrawPolygon @0x100020e0 */
    int  nDrawEnabled;                     /* +0x60 gate flag (decompiled
                                              GxDriverApi.nDrawEnabled) */
    int  (*pBlitSurface)(int, int, int, void *, int, int, int, int, int);
                                           /* +0x64 -> gxBlitSurface @0x10002950 */
    void (*pSetOrigin)(int);               /* +0x68 -> gxSetOrigin @0x10001f90 */
    void (*pDrawTriangle)(void *, int);    /* +0x6c -> gxDrawTriangle @0x10002000 */
    void (*pDrawLine)(void *, void *, int);/* +0x70 -> gxDrawLine @0x10002060 */
    void (*pDrawTriUV)(void *, void *, void *, int, void *);
                                           /* +0x74 -> gxDrawTriUV @0x100021f0 */
    void (*pDrawQuad)(void *, void *, void *, void *, int, void *);
                                           /* +0x78 -> gxDrawQuad @0x10002540 */
    int  nSoftwareMode;                    /* +0x7c (driver forces poly coords to int) */
} GxDriverApi;                             /* 0x80 bytes driver-owned */

/* maniac-side extension of GxDriverApi (struct @0x45eb40, 136 bytes):
 * +0x80 hDriverModule, +0x84 nDriverActive */
typedef struct GxDriver {
    GxDriverApi api;
    HMODULE     hDriverModule;   /* +0x80 */
    int         nDriverActive;   /* +0x84 */
} GxDriver;

/* Active driver state (defined in gx.c; mirrors the maniac-side
 * extension of GxDriverApi @0x45eb40). */
extern GxDriver g_driver;

/* Reimplementation of maniac gxInit @0x4332f0: pSetMode dispatch. */
int  gxInit(GxMode *mode);
/* Reimplementation of maniac gxLoadDriver @0x432ea0; the known GXSOFT path
 * is used instead of the deferred registry-selection branch. */
int  gxLoadDriver(char *driverName);
/* Reimplementation of maniac gxUnloadDriver @0x433280 (narrowed: no registry). */
int  gxUnloadDriver(void);
/* Reimplementation of maniac presentFrame @0x410310. */
void presentFrame(int texture);
/* Reimplementation of maniac gxLoadTpgFile @0x416060 (tpg -> gxLoadTexture). */
int gxLoadTpgFile(LPCSTR path);

/* Vertex and texture/UV record types for the indexed-draw wrappers. Layouts
 * match the Ghidra structs GxVert / GxColorUv (see gxDrawPolygon @0x433440
 * and gxDrawQuadColor @0x414470). */
typedef struct GxVert {
    int x;   /* +0x00 8.8 fixed-point (software mode) / float (hardware mode) */
    int y;   /* +0x04 */
    int z;   /* +0x08 depth (gxDrawQuadColor sets 0) */
    unsigned char r; /* +0x0c vertex color (GXSOFT reads +0xc..+0xe) */
    unsigned char g; /* +0x0d */
    unsigned char b; /* +0x0e */
    unsigned char a; /* +0x0f */
} GxVert;    /* 0x10 bytes */

typedef struct GxColorUv {
    void          *pTexture;   /* +0x00 texture node (record word 0) */
    void          *pParam5;    /* +0x04 */
    int            pad;        /* +0x08 */
    unsigned short U;          /* +0x0c */
    unsigned short V;          /* +0x0e */
    unsigned short gwU;        /* +0x10 */
    unsigned short V2;         /* +0x12 */
    unsigned short gwU2;       /* +0x14 */
    unsigned short hV;         /* +0x16 */
    unsigned short U2;         /* +0x18 */
    unsigned short hV2;        /* +0x1a */
} GxColorUv;                   /* 0x1c */

/* 2D vector used by the math helpers and the EventObject zone test
 * (objContainsPoint @0x414bb0 builds one via gxVec2SetAngleZero). */
typedef struct GxVec2 {
    float x;   /* +0x00 (polar: length) */
    float y;   /* +0x04 (polar: angle) */
} GxVec2;      /* 8 bytes */

/* --- 2D vector helpers (originals verified in Ghidra) --- */
void gxVec2SetAngleZero(GxVec2 *pVec);   /* @0x434f90 set to unit X {1,0} */
void gxVec2Set(GxVec2 *pVec, float x, float y);            /* @0x434fa0 */
void gxVec2FromPolar(GxVec2 *pOut, const GxVec2 *pPolar);  /* @0x434fc0 */
void mathVec2Polar(GxVec2 *pOut, const GxVec2 *pIn);       /* @0x435060 (scene.c) */

/* Maniac-side GX wrapper cluster (thin dispatches through GxDriverApi).
 * Each matches the maniac.exe function at the noted address. */
int  gxGetMode(GxMode *mode);              /* @0x433310 */
int  gxSnooze(void);                       /* @0x433330 */
int  gxFlip(void);                         /* @0x433340 */
int  gxClearScreen(int clearMode, int color); /* @0x433350 */
int  gxSetViewport(void *rect);            /* @0x433370 */
int  gxGetViewport(void *rect);            /* @0x433390 */
int  gxResetState(void);                   /* @0x4333b0 */
int  gxLoadTexture(int mode, int reserved, char *name, void *data,
                   void *palette);         /* @0x4333d0 */
int  gxCreateSurface(char *path);          /* @0x433420 */
void gxDrawPolygon(GxVert *v0, GxVert *v1, GxVert *v2, GxVert *v3, int flags,
                   GxColorUv *colorUv);    /* @0x433440 */
int  gxBlitSurface(int a, int b, int c, void *tex, int x, int y,
                   int w, int h, int h2);  /* @0x433580 */
void gxSetOrigin(int packedOrigin);        /* @0x4335d0 */
void gxDrawTriangle(void *v0, int color);  /* @0x4335f0 */
void gxDrawLine(void *v0, void *v1, int color); /* @0x433610 */
void gxDrawTriUV(void *v0, void *v1, void *v2, int color, void *uv); /* @0x433640 */
void gxDrawQuad(void *v0, void *v1, void *v2, void *v3, int color,
                 void *uv);                 /* @0x433670 */
void gxDrawQuadColor(void *tex,int x0,int y0,int x1,int y1,int u0,int v0,int u1,int v1); /* @0x414470 */

#endif /* GX_H */