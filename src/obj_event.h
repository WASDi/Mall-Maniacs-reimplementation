#ifndef OBJ_EVENT_H
#define OBJ_EVENT_H

#include "gx.h"
#include "scene.h"

/* obj_event.h — EventObject registry + point-in-zone tests
 * (maniac.exe 0x414xxx). Cluster A of the obj subsystem.
 * EventObjects are the named zone/trigger objects ("AR00", "IN00",
 * shopping zones, ...) whose 4-byte name acts as the integer id
 * used by objFindById. Creation via eloadCmd @0x406cb0. */

typedef unsigned char obj_byte;

#ifndef OBJ_EVENT_BYTE_DEFINED
#define OBJ_EVENT_BYTE_DEFINED
#ifndef byte
typedef unsigned char byte;
#endif
#endif

/* One 0x20-byte boundary line of a zone polygon. Coordinates are relative
 * to the EventObject origin (+0x38/+0x3c). Layout from objContainsPoint
 * @0x414bb0 disassembly. */
typedef struct ObjLine {
    float x1;        /* +0x00 */
    float y1;        /* +0x04 */
    float x2;        /* +0x08 */
    float y2;        /* +0x0c */
    float nx;        /* +0x10 unit normal x */
    float ny;        /* +0x14 unit normal y */
    struct ObjLine *pNext;   /* +0x18 */
    struct ObjLine *pPrev;   /* +0x1c */
} ObjLine;           /* 0x20 */

/* EventObject: 0x50-byte zone/trigger object. Layout from objContainsPoint
 * @0x414bb0, objContainsPoint3D @0x414b60, objFindById @0x414a90,
 * objHashRehash @0x4148c0 and sceneObjCtor4 @0x414730. */
typedef struct EventObject {
    int      field_00;      /* +0x00 */
    ObjLine *pLineList;     /* +0x04 zone polygon line list */
    int      nId;           /* +0x08 id (4-byte name tag), hash key */
    int      field_0c;      /* +0x0c */
    int      field_10;      /* +0x10 */
    int      field_14;      /* +0x14 */
    int      field_18;      /* +0x18 */
    int      field_1c;      /* +0x1c */
    int      field_20;      /* +0x20 */
    int      field_24;      /* +0x24 */
    int      field_28;      /* +0x28 */
    int      field_2c;      /* +0x2c */
    int      field_30;      /* +0x30 */
    int      field_34;      /* +0x34 */
    float    flOriginX;     /* +0x38 zone origin x */
    float    flOriginZ;     /* +0x3c zone origin z */
    float    flHeightA;     /* +0x40 vertical bound a */
    float    flHeightB;     /* +0x44 vertical bound b */
    struct EventObject *pHashNext;  /* +0x48 id-hash chain next */
    struct EventObject *pHashPrev;  /* +0x4c id-hash chain prev */
} EventObject;              /* 0x50 */

#define OBJ_HASH_BUCKETS 0xff

/* g_apGrabbMesh @0x4583b8 — SceneObjTypeDef table indexed by world item id
 * (0x2c-byte stride: index = id * 11 in the original dword addressing). */
extern SceneObjTypeDef g_apGrabbMesh[31];           /* @0x4583b8 */

/* objHashRemoveFree @0x414990 — unlink the EventObject from its id-hash
 * bucket (bucket head, +0x48 next and +0x4c prev links), then objHashDtor
 * and free the object. */
void objHashRemoveFree(EventObject *pObj);           /* @0x414990 */

/* objFindById @0x414a90 — return the (nIndex+1)-th EventObject with
 * id == nId in the g_apObjHashBuckets table (bucket = id % 0xff, chain
 * +0x48). Lazily zeroes the bucket table on first use. Returns NULL when
 * nIndex < 0 or no match. */
EventObject *objFindById(int nId, int nIndex);

/* objHashNextSame @0x414a40 — next EventObject with the SAME 4-byte id in
 * the id-hash chain, or NULL when the chain ends or the next object has a
 * different id. */
EventObject *objHashNextSame(EventObject *pObj);

/* objContainsPoint @0x414bb0 — 2D point-in-zone test over the polygon
 * line list: ray-casts a horizontal line at the point, finds the closest
 * crossing segment (squared distance) and tests its half-plane normal. */
int objContainsPoint(EventObject *pObj, float flX, float flY);

/* objContainsPoint3D @0x414b60 — vertical bound check (flZ against
 * +0x40/+0x44 with +/- 10) then objContainsPoint on (flX, flY). */
int objContainsPoint3D(EventObject *pObj, float flX, float flY, float flZ);

/* lineRecordNormal @0x414620 — unit normal (nx,ny) of the line segment,
 * the polar direction rotated by -90 degrees (g_flHalfPi @0x44b270). */
void lineRecordNormal(ObjLine *pLine);

/* lineRecordCtor @0x4145d0 — store x1,y1,x2,y2, clear the list links and
 * compute the normal. Returns pLine. */
ObjLine *lineRecordCtor(ObjLine *pLine, float flX1, float flY1, float flX2, float flY2);

/* objSubDtor @0x414680 — recursively free a line-record chain (+0x18). */
void objSubDtor(ObjLine *pLine);

/* objHashDtor @0x414760 — free an EventObject: clear its bucket head,
 * objSubDtor+free the line list, recurse the +0x48 hash chain. */
void objHashDtor(EventObject *pObj);

/* objHashRegister @0x4148f0 — head-insert into bucket id % 0xff of
 * g_apObjHashBuckets (lazily zeroed on first use, g_nObjHashInit). */
void objHashRegister(EventObject *pObj);

/* objHashFreeAll @0x414950 — objHashDtor + free every bucket chain and
 * zero the buckets (the init flag stays set). */
void objHashFreeAll(void);

/* sceneObjCtor4 @0x414700 — zero the 0x50-byte EventObject and set
 * nId (+0x08), origin (+0x38/+0x3c) and vertical bounds (+0x40/+0x44). */
/* sceneObjCtor3 @0x4146a0 — zero the 0x50-byte EventObject and set nId,
 * origin (+0x38/+0x3c) and both vertical bounds (+0x40/+0x44 = flHeight).
 * Landed thrown-item pickups (itemThrowUpdate) and level-event bonus
 * items use it. */
void sceneObjCtor3(EventObject *pObj, int nId, float flX, float flY,
                   float flHeight);

void sceneObjCtor4(EventObject *pObj, int nId, float flX, float flY,
                   float flHeight, float flHeight2);

/* eventObjAddLine @0x4147c0 — append a zone boundary line given in
 * absolute coordinates: the stored record is relative to the object
 * origin, head-inserted into the +0x04 line list. */
void eventObjAddLine(EventObject *pObj, float flX1, float flY1,
                     float flX2, float flY2);

/* eloadCmd @0x406cb0 — startup-script/console "eload <file>": parse the
 * DFF-format .eo file, objHashFreeAll, then one EventObject per block
 * ("_<digits>" keys -> numeric id via fmtAtoi, other keys use the first
 * dword of the 4-char key; id 0x1f is the skipped Burger object), with
 * its "line[%d]" polygon records and up to 5 "values" ints (+0x10..).
 * Returns 0. */
int eloadCmd(int nContext, LPCSTR pszArgs);

/* objFindByIdInRange @0x414af0 — (nIndex+1)-th EventObject whose id is in
 * [nIdMin, nIdMax], scanned in ascending id order; 0 when none. */
EventObject *objFindByIdInRange(int nIdMin, int nIdMax, int nIndex);

#endif /* OBJ_EVENT_H */
