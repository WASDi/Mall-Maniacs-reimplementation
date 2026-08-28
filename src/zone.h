#ifndef ZONE_H
#define ZONE_H

#include "obj.h"

/* zone.h — AR/IN zone-connection nodes (maniac.exe 0x42b410..0x42b890).
 * roundStartInit creates one per "AR%02.2d"/"IN%02.2d" EventObject pair
 * after levelSetup; the nodes record which detail-meshes of the level
 * scene touch each room-entering AR zone so room interiors can be culled
 * while the player is elsewhere (zoneConnUpdateCulling @0x42b8f0).
 * Freed by roundTeardown via zoneConnUnlink @0x42b890. */

/* 0x1c-byte zone-connection node (layout from zoneConnCtor @0x42b410
 * disassembly). Head-inserted into the g_pZoneConnHead list. */
typedef struct ZoneConn {
    int              field_00;         /* +0x00 */
    struct ZoneConn *pPrev;            /* +0x04 previous head (list is LIFO) */
    struct ZoneConn *pNext;            /* +0x08 next node */
    int              nCount;           /* +0x0c detail-mesh indices collected */
    int             *pMeshIdx;         /* +0x10 nCount ints (memPoolAlloc) */
    EventObject     *pInZone;          /* +0x14 the IN-%02.2d zone object */
    void            *pDetailLevels;    /* +0x18 SceneDetailGrid* */
} ZoneConn;                            /* 0x1c */

/* List heads (zoneConnCtor writes them directly). */
extern void *g_pZoneConnHead;   /* @0x45e5e8 */
extern void *g_pZoneConnTail;   /* @0x45e5ec */
extern void *g_zoneConn;        /* @0x45e5e4 scratch cleared at ctor start */

/* zoneConnCtor @0x42b410 — build a 0x1c-byte connection node: collect the
 * intersecting detail-mesh indices (counting pass), allocate the index
 * array sized to that count, refill it, then prepend to the list.
 * Returns the node. Original __thiscall RET 0xc. */
void *zoneConnCtor(ZoneConn *pConn, EventObject *pInZone, EventObject *pArZone,
                   void *pDetailLevels);

/* zoneConnCollectMeshes @0x42b490 — walk the detail grid's filled column
 * cells (pDetailLevels->nColsFilled entries of pCells), test each mesh's
 * polygon vertices against the AR zone (objContainsPoint3D) and count
 * (pFill != 0) or store (pFill == 0) the matching cell indices. */
void zoneConnCollectMeshes(ZoneConn *pConn, EventObject *pArZone, int nFill);

#endif /* ZONE_H */
