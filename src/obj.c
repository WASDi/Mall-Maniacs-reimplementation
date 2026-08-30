#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "obj.h"
#include "gx.h"
#include "scene.h"
#include "config.h"
#include "util.h"
#include "pool.h"
#include "time.h"
#include "zone.h"
#include "player_physics.h"
#include "stubs.h"

extern WorldNode *g_pObjHead; /* @0x4550d4 (defined below) */

/* =====================================================================
 * obj.c — EventObject registry: creation (.eo load), id-hash and
 * point-in-zone tests. Reimplemented from the verified Ghidra
 * decompilations/disassemblies of lineRecordCtor @0x4145d0,
 * lineRecordNormal @0x414620, objSubDtor @0x414680, objHashDtor
 * @0x414760, sceneObjCtor4 @0x414700, eventObjAddLine @0x4147c0,
 * objHashRehash @0x4148c0, objHashRegister @0x4148f0, objHashFreeAll
 * @0x414950, objFindById @0x414a90, objContainsPoint @0x414bb0,
 * objContainsPoint3D @0x414b60 and eloadCmd @0x406cb0.
 * ===================================================================== */

EventObject *g_apObjHashBuckets[OBJ_HASH_BUCKETS];  /* @0x459500 */
int g_nObjHashInit;                                 /* @0x4598fc */

static const float g_flHalfPi = 1.5707964f;  /* @0x44b270 (bytes DB 0F C9 3F) */

/* g_apGrabbMesh @0x4583b8 — SceneObjTypeDef table indexed by world item
 * id (0x2c-byte stride, ids 1..0x1e), read by playerAiGrabItem.
 * TODO: filled by the object loader (not yet decoded); stays zeroed
 * so pickups allocate a sub-channel-less mesh placeholder. */
SceneObjTypeDef g_apGrabbMesh[31];                 /* @0x4583b8 */

/* objFindById @0x414a90 — bucket = nId % 0xff; walk the +0x48 chain, walk the +0x48 chain,
 * match +0x08 == nId; the (nIndex+1)-th match is returned. The bucket
 * table is lazily zeroed on first use (g_nObjHashInit @0x4598fc). */
EventObject *objFindById(int nId, int nIndex) /* @0x414a90 */
{
    EventObject *p;
    if (nIndex < 0) {
        return NULL;
    }
    if (g_nObjHashInit == 0) {
        g_nObjHashInit = 1;
        memset(g_apObjHashBuckets, 0, sizeof(g_apObjHashBuckets));  /* rep stosd 0x414aa3 */
    }
    for (p = g_apObjHashBuckets[nId % OBJ_HASH_BUCKETS]; p != NULL; p = p->pHashNext) {
        if (p->nId == nId) {
            if (nIndex == 0) {          /* 0x414ada: TEST EDX,EDX (pre-decrement) */
                return p;
            }
            nIndex--;
        }
    }
    return NULL;
}

/* objUpdateAll @0x4055f0 — clear every world node's per-frame state
 * (+0x40 scratch and +0x18 channels-dirty flag), then run the world
 * physics/fire passes (physics twice, matching the original order; the
 * g_nMovieFrame 399..500 guards only wrap nopDebugStub no-ops). */
void objUpdateAll(void) /* @0x4055f0 */
{
    WorldNode *pNode;

    for (pNode = g_pObjHead; pNode != NULL; pNode = pNode->pPrev) { /* +0x00 walk toward tail @0x4055f6 */
        pNode->flImpulse = 0.0f;  /* +0x40 @0x4055fc */
        pNode->field_18 = 0;  /* +0x18 @0x405601 */
    }
    objUpdatePhysics();       /* @0x405680 @0x405611 */
    objUpdateFire();          /* @0x405e10 @0x405621 */
    objUpdatePhysics();       /* @0x40562c */
}

/* objFindByIdInRange @0x414af0 — (nIndex+1)-th EventObject whose id lies in
 * [nIdMin, nIdMax], scanning ids ascending and each bucket chain in order.
 * Shares objFindById's lazy bucket initialization. */
EventObject *objFindByIdInRange(int nIdMin, int nIdMax, int nIndex) /* @0x414af0 */
{
    int nId;
    EventObject *p;

    if (nIndex < 0) {
        return NULL;                                   /* @0x414b5c */
    }
    if (g_nObjHashInit == 0) {
        g_nObjHashInit = 1;
        memset(g_apObjHashBuckets, 0, sizeof(g_apObjHashBuckets)); /* @0x414b03 */
    }
    for (nId = nIdMin; nId <= nIdMax; nId++) {         /* @0x414b0f */
        for (p = g_apObjHashBuckets[nId % OBJ_HASH_BUCKETS]; p != NULL;
             p = p->pHashNext) {                       /* @0x414b17 */
            if (p->nId == nId) {                       /* +0x08 @0x414b1f */
                if (nIndex == 0) {
                    return p;
                }
                nIndex--;
            }
        }
    }
    return NULL;                                       /* @0x414b57 */
}

/* objHashNextSame @0x414a40 — next EventObject with the same 4-byte id in
 * the +0x48 id-hash chain; NULL when the chain ends or the next object has
 * a different id (buckets are id % 0xff, so a chain can mix ids). */
EventObject *objHashNextSame(EventObject *pObj) /* @0x414a40 */
{
    EventObject *pNext = pObj->pHashNext;             /* @0x414a40 */
    if (pNext == NULL || pNext->nId != pObj->nId) {   /* @0x414a46 */
        return NULL;
    }
    return pNext;
}

/* objContainsPoint @0x414bb0 — point-in-polygon over the zone's line
 * list. For every segment whose y-range brackets the point, solve the
 * crossing x on the horizontal ray, keep the closest crossing (squared
 * distance, ties keep the first) and record the half-plane side from the
 * segment normal. Returns 1 when the closest crossing is inside. The
 * gxVec2SetAngleZero scratch vector mirrors the original's local setup
 * (its value is never read back). */
int objContainsPoint(EventObject *pObj, float flX, float flY) /* @0x414bb0 */
{
    float flBestD2 = -1.0f;
    int nInside = 0;
    GxVec2 vScratch;
    float fDx = flX - pObj->flOriginX;      /* +0x38 */
    float fDy = flY - pObj->flOriginZ;      /* +0x3c */
    ObjLine *l;

    gxVec2SetAngleZero(&vScratch);          /* @0x434f90 */
    for (l = pObj->pLineList; l != NULL; l = l->pNext) {
        if (l->y1 != l->y2 &&
            (l->y1 <= fDy || l->y2 <= fDy) &&
            (fDy <= l->y1 || fDy <= l->y2)) {
            float fCross;
            if (l->x1 == l->x2) {
                fCross = l->x1;             /* vertical segment */
            } else {
                float slope = (l->y2 - l->y1) / (l->x2 - l->x1);
                fCross = (fDy - (l->y2 - slope * l->x2)) / slope;
            }
            {
                float fD = fDx - fCross;
                fD = fD * fD;
                if (flBestD2 < 0.0f || fD < flBestD2) {
                    flBestD2 = fD;
                    if (0.0f < (fDy - l->y1) * l->ny + (fDx - l->x1) * l->nx) {
                        nInside = 1;
                    } else {
                        nInside = 0;
                    }
                }
            }
        }
    }
    return nInside;
}

/* objContainsPoint3D @0x414b60 — flZ is the vertical axis: accept when
 * flZ-10 <= +0x40 and +0x44 <= flZ+10 (g_fl_10 @0x44b44c = 10.0f), then
 * delegate the horizontal test to objContainsPoint. */
int objContainsPoint3D(EventObject *pObj, float flX, float flY, float flZ) /* @0x414b60 */
{
    if (flZ - 10.0f <= pObj->flHeightA && pObj->flHeightB <= flZ + 10.0f) {
        if (objContainsPoint(pObj, flX, flY) != 0) {
            return 1;
        }
    }
    return 0;
}

/* lineRecordNormal @0x414620 — unit normal of the segment: polar of the
 * direction (x2-x1, y2-y1), angle -= PI/2 (g_flHalfPi @0x44b270),
 * length fixed to 1.0, then back from polar into +0x10/+0x14. */
void lineRecordNormal(ObjLine *pLine) /* @0x414620 */
{
    GxVec2 vDir;
    GxVec2 vNormal;

    gxVec2Set(&vDir, pLine->x2 - pLine->x1, pLine->y2 - pLine->y1);  /* @0x434fa0 */
    mathVec2Polar(&vNormal, &vDir);                                  /* @0x435060 */
    vNormal.y -= g_flHalfPi;                                       /* @0x44b270 */
    vNormal.x = 1.0f;
    gxVec2FromPolar(&vNormal, &vNormal);                             /* @0x434fc0 */
    pLine->nx = vNormal.x;
    pLine->ny = vNormal.y;
}

/* lineRecordCtor @0x4145d0 — store the segment, clear the list links,
 * compute the unit normal. */
ObjLine *lineRecordCtor(ObjLine *pLine, float flX1, float flY1, float flX2, float flY2) /* @0x4145d0 */
{
    pLine->x1 = flX1;
    pLine->y1 = flY1;
    pLine->x2 = flX2;
    pLine->y2 = flY2;
    pLine->pNext = NULL;                 /* +0x18 */
    pLine->pPrev = NULL;                 /* +0x1c */
    lineRecordNormal(pLine);             /* @0x414620 */
    return pLine;
}

/* objSubDtor @0x414680 — recursively free the +0x18 line chain. */
void objSubDtor(ObjLine *pLine) /* @0x414680 */
{
    if (pLine->pNext != NULL) {
        objSubDtor(pLine->pNext);
        memFreeDirect(pLine->pNext);     /* @0x43dd37 */
    }
}

/* objHashDtor @0x414760 — clear the bucket head when this object is it,
 * free the line list via objSubDtor, then recurse down the +0x48 hash
 * chain (each caller frees its own node after the recursion). */
void objHashDtor(EventObject *pObj) /* @0x414760 */
{
    int nBucket = pObj->nId % OBJ_HASH_BUCKETS;
    if (g_apObjHashBuckets[nBucket] == pObj) {
        g_apObjHashBuckets[nBucket] = NULL;
    }
    if (pObj->pLineList != NULL) {       /* +0x04 line-list head */
        objSubDtor(pObj->pLineList);     /* @0x414680 */
        memFreeDirect(pObj->pLineList);
    }
    if (pObj->pHashNext != NULL) {       /* +0x48 chain */
        objHashDtor(pObj->pHashNext);
        memFreeDirect(pObj->pHashNext);
    }
}

/* objHashRemoveFree @0x414990 — unlink an EventObject from its bucket
 * chain (fix the bucket head / +0x48 / +0x4c neighbours), zero both links,
 * run objHashDtor and free the record itself. */
void objHashRemoveFree(EventObject *pObj) /* @0x414990 */
{
    int nBucket = pObj->nId % OBJ_HASH_BUCKETS;

    if (g_apObjHashBuckets[nBucket] == pObj) {
        g_apObjHashBuckets[nBucket] = pObj->pHashNext;  /* @0x4149b2 */
    }
    if (pObj->pHashNext != NULL) {                      /* +0x48 */
        pObj->pHashNext->pHashPrev = pObj->pHashPrev;   /* @0x4149c0 */
    }
    if (pObj->pHashPrev != NULL) {                      /* @0x4149c6 */
        pObj->pHashPrev->pHashNext = pObj->pHashNext;   /* @0x4149cd */
    }
    if (pObj != NULL) {                                 /* @0x4149d3 */
        pObj->pHashNext = NULL;                         /* @0x40? @0x4149d5 */
        pObj->pHashPrev = NULL;                         /* @0x4149d8 */
        objHashDtor(pObj);                              /* @0x414760 @0x4149df */
        memFreeDirect(pObj);                            /* @0x43dd37 @0x4149e5 */
    }
}

/* objHashRegister @0x4148f0 — lazily zero the bucket table, then
 * head-insert into bucket nId % 0xff (+0x48 next, +0x4c prev). */
void objHashRegister(EventObject *pObj) /* @0x4148f0 */
{
    int nBucket;
    if (g_nObjHashInit == 0) {
        memset(g_apObjHashBuckets, 0, sizeof(g_apObjHashBuckets));  /* 0xff rep stosd */
        g_nObjHashInit = 1;
    }
    nBucket = pObj->nId % OBJ_HASH_BUCKETS;
    pObj->pHashNext = g_apObjHashBuckets[nBucket];
    g_apObjHashBuckets[nBucket] = pObj;
    if (pObj->pHashNext != NULL) {
        pObj->pHashNext->pHashPrev = pObj;
    }
}

/* objHashFreeAll @0x414950 — walk the bucket array (0x459500..0x4598fc),
 * objHashDtor + free each chain head and zero the slot. The init flag
 * stays set. Called from roundTeardown @0x40ad1f and eloadCmd. */
void objHashFreeAll(void) /* @0x414950 */
{
    int i;
    if (g_nObjHashInit == 0) {
        return;
    }
    for (i = 0; i < OBJ_HASH_BUCKETS; i++) {
        if (g_apObjHashBuckets[i] != NULL) {
            objHashDtor(g_apObjHashBuckets[i]);      /* @0x414760 */
            memFreeDirect(g_apObjHashBuckets[i]);
            g_apObjHashBuckets[i] = NULL;
        }
    }
}

/* sceneObjCtor4 @0x414700 — zero the whole 0x50-byte object, then set
 * nId, origin and both vertical bounds (entry verified at 0x414700 in
 * the eloadCmd call @0x406f55; Ghidra had split a stray label at
 * 0x414730 inside this body). */
void sceneObjCtor4(EventObject *pObj, int nId, float flX, float flY,
                   float flHeight, float flHeight2) /* @0x414700 */
{
    pObj->field_00 = 0;
    pObj->pLineList = NULL;
    pObj->nId = nId;
    pObj->field_0c = 0;
    pObj->field_10 = 0;
    pObj->field_14 = 0;
    pObj->field_18 = 0;
    pObj->field_1c = 0;
    pObj->field_20 = 0;
    pObj->field_24 = 0;
    pObj->field_28 = 0;
    pObj->field_2c = 0;
    pObj->field_30 = 0;
    pObj->field_34 = 0;
    pObj->flOriginX = flX;
    pObj->flOriginZ = flY;
    pObj->flHeightA = flHeight;
    pObj->flHeightB = flHeight2;
    pObj->pHashNext = NULL;
    pObj->pHashPrev = NULL;
}

/* eventObjAddLine @0x4147c0 — the coordinates arrive absolute (origin +
 * .eo line offset); the stored record is relative to the object origin.
 * New records are head-inserted into the +0x04 list (+0x18 next,
 * +0x1c prev back-link on the old head). */
void eventObjAddLine(EventObject *pObj, float flX1, float flY1,
                     float flX2, float flY2) /* @0x4147c0 */
{
    ObjLine *pLine = (ObjLine *)malloc(0x20);        /* operator_new @0x43dd42 */
    if (pLine == NULL) {
        return;
    }
    lineRecordCtor(pLine,                            /* @0x4145d0 */
                   flX1 - pObj->flOriginX, flY1 - pObj->flOriginZ,
                   flX2 - pObj->flOriginX, flY2 - pObj->flOriginZ);
    pLine->pNext = pObj->pLineList;                  /* +0x18 */
    if (pObj->pLineList != NULL) {
        pObj->pLineList->pPrev = pLine;              /* +0x1c */
    }
    pObj->pLineList = pLine;                         /* +0x04 */
}

/* eloadCmd @0x406cb0 — "eload <file>": parse the .eo DFF file into a
 * local ConfigEnv, clear the previous object set, then per top-level
 * node build one EventObject. Keys: "_<digits>" -> fmtAtoi(id+1);
 * other keys keep the first dword of the 4-char id (%-4.4s / %4.4s
 * @0x44e868/@0x44e860). Id 0x1f is skipped ("Burger EventObject found
 * and ignored." @0x44e838). "h2"/"h"/"y"/"x" set the ctor floats,
 * "line[%d]" the polygon records (absolute coords, made relative by
 * eventObjAddLine) and "values" up to 5 ints at +0x10. The parsed node
 * tree is freed afterwards (token list intentionally left, as in the
 * original). */
int eloadCmd(int nContext, LPCSTR pszArgs) /* @0x406cb0 */
{
    ConfigEnv env;
    ConfigNode *pNode;
    char szFile[100];
    char szKey[100];
    char szId[5];

    (void)nContext;
    configEnvCtor(&env);                             /* @0x4357c0 */
    if (fmtSscanf(pszArgs, "%s", szFile) == -1) {    /* g_sz_s @0x44e70c */
        nopDebugStub();                              /* @0x44e8bc */
        mStringFree(&env.name);
        return 0;
    }
    if (configParseFile(&env, szFile) < 0) {         /* @0x435890 */
        nopDebugStub();                              /* @0x44e8a4 */
        mStringFree(&env.name);
        return 0;
    }
    nopDebugStub();                                  /* @0x44e888 */
    objHashFreeAll();                                /* @0x414950 */
    nopDebugStub();                                  /* "Loading EventObjects" @0x44e870 */

    for (pNode = configNextNode(&env, NULL); pNode != NULL;
         pNode = configNextNode(&env, pNode)) {
        EventObject *pEvent;
        int nId;
        int i;

        fmtSprintf(szId, "%-4.4s", mStringCStr(&pNode->key));
        if (szId[0] == '_') {
            nId = fmtAtoi(szId + 1);                 /* @0x43e75c */
        } else {
            fmtSprintf(szId, "%4.4s", mStringCStr(&pNode->key));
            nId = *(int *)szId;                      /* id = first dword of the name */
        }
        if (*(int *)szId == 0x1f) {
            nopDebugStub();                          /* "Burger EventObject found and ignored." @0x44e838 */
            continue;
        }

        pEvent = (EventObject *)malloc(0x50);        /* operator_new @0x43dd42 */
        if (pEvent != NULL) {
            float flH2 = (float)configEnvGetDouble2(&env, pNode, "h2");
            float flH  = (float)configEnvGetDouble2(&env, pNode, "h");
            float flY  = (float)configEnvGetDouble2(&env, pNode, "y");
            float flX  = (float)configEnvGetDouble2(&env, pNode, "x");
            sceneObjCtor4(pEvent, nId, flX, flY, flH, flH2);   /* @0x414700 */
        }

        for (i = 0; ; i++) {
            ConfigNode *pLineNode;
            float flY2, flX2, flY1, flX1;
            float flOrgX = (pEvent != NULL) ? pEvent->flOriginX : 0.0f;
            float flOrgY = (pEvent != NULL) ? pEvent->flOriginZ : 0.0f;

            fmtSprintf(szKey, "line[%d]", i);        /* s_line__d @0x44e81c */
            pLineNode = configEnvGetValue(&env, pNode, szKey);   /* @0x436560 */
            if (pLineNode == NULL) {
                break;
            }
            flY2 = (float)configEnvGetDouble2(&env, pLineNode, "y2") + flOrgY;
            flX2 = (float)configEnvGetDouble2(&env, pLineNode, "x2") + flOrgX;
            flY1 = (float)configEnvGetDouble2(&env, pLineNode, "y1") + flOrgY;
            flX1 = (float)configEnvGetDouble2(&env, pLineNode, "x1") + flOrgX;
            eventObjAddLine(pEvent, flX1, flY1, flX2, flY2);     /* @0x4147c0 */
        }

        {
            ConfigNode *pValue = configEnvGetValue(&env, pNode, "values");
            int *pnOut = (pEvent != NULL) ? &pEvent->field_10 : NULL;
            for (i = 0; i < 5 && pValue != NULL; i++) {
                if (pnOut != NULL) {
                    *pnOut = (int)configEnvGetDouble(pValue);    /* @0x4369f0 + __ftol @0x4070a4 */
                    pnOut++;
                }
                pValue = configNextNode(&env, pValue);
            }
        }

        objHashRegister(pEvent);                     /* @0x4148f0 */
    }

    configEnvFreeChildren(&env);                     /* @0x4368a0 */
    mStringFree(&env.name);
    return 0;
}

/* =====================================================================
 * World-node cluster (playerSetupRound sub-objects)
 * ===================================================================== */

WorldNode *g_pObjHead;  /* @0x4550d4 world-object list head */

static const float g_flPi = 3.1415925f;        /* @0x44b2d0 (bytes D0 0F 49 40) */
static const double g_dblAngleScale = 3.0547e-05; /* @0x44b2c8 (bytes 10 00 10 00 10 00 00 3F) */

/* objListPush @0x4055c0 — head-insert a world node into g_pObjHead.
 * The node's +0x00/+0x04 links form the list chain. */
void objListPush(WorldNode *pNode) /* @0x4055c0 */
{
    if (g_pObjHead != NULL) {
        g_pObjHead->pNext = pNode;
        pNode->pPrev = g_pObjHead;
        g_pObjHead = pNode;
        return;
    }
    g_pObjHead = pNode;
}

/* worldNodeCtor @0x402a20 — init a 0x48-byte world node: zero the link and
 * sub-struct fields, vPos = {(float)nZ, (float)nX} (ground plane stored as
 * {z, x}), copy it to vPosB, scale = nScale*g_flPi*g_dblAngleScale into
 * +0x30/+0x34/+0x38, parent at +0x44, then objListPush. */
WorldNode *worldNodeCtor(void *pMem, int nX, int nY, int nZ, short nScale,
                          void *pParent) /* @0x402a20 */
{
    WorldNode *pNode = (WorldNode *)pMem;
    (void)nY;
    float flScale;

    gxVec2SetAngleZero(&pNode->vPos);       /* +0x20 @0x434f90 @0x402a2a */
    gxVec2SetAngleZero(&pNode->vPosB);      /* +0x28 @0x402a34 */
    pNode->vPos.x = (float)nZ;              /* @0x402a45 */
    pNode->pPrev = NULL;                    /* @0x402a40 */
    pNode->pNext = NULL;                    /* @0x402a42 */
    pNode->vPos.y = (float)nX;              /* +0x24 @0x402a57 */
    pNode->pShotList = NULL;                /* +0x10 @0x402a54 */
    pNode->pChildMeshHead = NULL;            /* +0x08 @0x402a51 */
    pNode->pTurretHead = NULL;               /* +0x0c @0x402a4e */
    pNode->field_3c = 0;                    /* @0x402a4b */
    pNode->field_14 = 0;                    /* @0x402a75 */
    pNode->field_1c = 0;                    /* @0x402a7b */
    pNode->vPosB.x = pNode->vPos.x;         /* @0x402a5f */
    pNode->vPosB.y = pNode->vPos.y;         /* @0x402a5c..0x402a61 */
    pNode->pParent = pParent;               /* +0x44 @0x402a78 */
    flScale = (float)nScale * g_flPi * (float)g_dblAngleScale; /* @0x44b2d0/0x44b2c8 @0x402a7e */
    pNode->flScaleA = flScale;              /* +0x30 @0x402a8a */
    pNode->flScaleB = flScale;              /* +0x38 @0x402a8d */
    pNode->flScaleC = flScale;              /* +0x34 @0x402a90 */
    objListPush(pNode);                     /* @0x4055c0 @0x402a93 */
    return pNode;
}

/* objDtor @0x402ab0 — unlink the node from g_pObjHead, free the shot list
 * at +0x10 (objShotListFree @0x406110 + memFreeDirect) and the two turret
 * sub-structs at +0x08/+0x0c (objTurretListFree @0x402b40 /
 * objTurretListFree2 @0x402b70 on their embedded lists) when present. */
void objDtor(WorldNode *pNode) /* @0x402ab0 */
{
    void *pSub;

    if (g_pObjHead == pNode) {                       /* @0x4550d4 @0x402ab8 */
        g_pObjHead = pNode->pPrev;                   /* @0x402abc */
    } else if (pNode->pNext != NULL) {               /* @0x402ac5 */
        pNode->pNext->pPrev = pNode->pPrev;          /* @0x402acc */
    }
    if (pNode->pPrev != NULL) {                      /* @0x402ad0 */
        pNode->pPrev->pNext = pNode->pNext;          /* @0x402ad6 */
    }
    if (pNode->pShotList != NULL) {                  /* +0x10 @0x402add */
        objShotListFree(pNode->pShotList);           /* @0x406110 @0x402ae6 */
        memFreeDirect(pNode->pShotList);             /* @0x43dd37 @0x402aeb */
    }
    pSub = pNode->pChildMeshHead;                    /* +0x08 @0x402af4 */
    if (pSub != NULL) {
        if (*(void **)((char *)pSub + 0x48) != NULL) { /* @0x402afb */
            objTurretListFree(1);                    /* @0x402b40 @0x402b04 */
        }
        memFreeDirect(pSub);                         /* @0x402b0a */
    }
    pSub = pNode->pTurretHead;                       /* +0x0c @0x402b12 */
    if (pSub != NULL) {
        if (*(void **)((char *)pSub + 0x18) != NULL) { /* @0x402b1a */
            objTurretListFree2(1);                   /* @0x402b70 @0x402b23 */
        }
        memFreeDirect(pSub);                         /* @0x402b29 */
    }
}

/* --- per-node queries (playerUpdateAI / pickup / camera paths) --- */

static const float g_flZero = 0.0f; /* @0x44b244 */
static const float g_flOne = 1.0f;  /* @0x44b260 */

/* objDistTo @0x404fe0 — Euclidean ground-plane distance between two nodes'
 * vPos (x vs x, y vs y slots). */
float objDistTo(WorldNode *pA, WorldNode *pB) /* @0x404fe0 */
{
    float flDx = pB->vPos.y - pA->vPos.y; /* +0x24 @0x404fe7 */
    float flDz = pB->vPos.x - pA->vPos.x; /* +0x20 @0x404fee */
    return sqrtf(flDx * flDx + flDz * flDz); /* @0x404ff5..0x404ffb */
}

/* objAngleTo @0x405010 — polar angle (atan2) of the vector from pA's vPos
 * to pB's vPos (mathVec2Polar .y over the {x, y} component delta). */
float objAngleTo(WorldNode *pA, WorldNode *pB) /* @0x405010 */
{
    GxVec2 vDelta;
    GxVec2 vPolar;

    gxVec2Set(&vDelta, pB->vPos.x - pA->vPos.x, pB->vPos.y - pA->vPos.y);
    mathVec2Polar(&vPolar, &vDelta); /* .y keeps the angle @0x405028 */
    return vPolar.y;
}

/* objAngleToPoint @0x405080 — polar angle (atan2) from pNode's vPos to the
 * point {flA, flB} (flA vs the +0x20 slot, flB vs the +0x24 slot). */
float objAngleToPoint(WorldNode *pNode, float flA, float flB) /* @0x405080 */
{
    GxVec2 vDelta;
    GxVec2 vPolar;

    gxVec2Set(&vDelta, flA - pNode->vPos.x, flB - pNode->vPos.y);
    mathVec2Polar(&vPolar, &vDelta); /* @0x405098 */
    return vPolar.y;
}

/* objMovePolar @0x404f10 — save vPos into vPosB, advance vPos by
 * fromPolar({flLen, flAng}) and store flLen/flAng into +0x3c/+0x38. */
void objMovePolar(WorldNode *pNode, float flLen, float flAng) /* @0x404f10 */
{
    GxVec2 vPolar;
    GxVec2 vCart;
    GxVec2 vSum;

    pNode->vPosB = pNode->vPos; /* +0x28 @0x404f24 */
    gxVec2Set(&vPolar, flLen, flAng);
    gxVec2FromPolar(&vCart, &vPolar);                 /* @0x404f2b */
    gxVec2Add(&vSum, &pNode->vPos, &vCart);           /* @0x404f3b */
    pNode->vPos = vSum;                               /* @0x404f45 */
    pNode->field_3c = flLen;   /* +0x3c @0x404f55 (raw float store) */
    pNode->flScaleC = flAng;   /* +0x38 @0x404f58 (raw float store) */
}

/* objSetAngle @0x404f70 — set the node heading: flScaleB = flScaleA,
 * flScaleA = flAngle, then recompute every child mesh's vWorldA from
 * {vPolar.x, vPolar.y + flScaleA} (saving the old one into vWorldB). */
void objSetAngle(WorldNode *pNode, float flAngle) /* @0x404f70 */
{
    ObjChildMesh *pMesh;
    GxVec2 vIn;

    pNode->flScaleB = pNode->flScaleA; /* +0x34 @0x404f83 */
    pNode->flScaleA = flAngle;         /* +0x30 @0x404f86 */
    for (pMesh = (ObjChildMesh *)pNode->pChildMeshHead; pMesh != NULL;
         pMesh = pMesh->pNext) {                       /* +0x48 @0x404fc3 */
        pMesh->vWorldB = pMesh->vWorldA;               /* +0x34 @0x404f94 */
        gxVec2Set(&vIn, pMesh->vPolar.x, pMesh->vPolar.y + pNode->flScaleA);
        gxVec2FromPolar(&pMesh->vWorldA, &vIn);        /* +0x2c @0x404fb3 */
    }
}

/* objPolarPosLookup @0x405140 — find pNode's turret entry whose nTypeId
 * matches nTypeId and return (int) of the world x component of
 * fromPolar({vPolar.x, flScaleA + vPolar.y}) + vPos (the +0x24 slot). */
int objPolarPosLookup(WorldNode *pNode, int nTypeId) /* @0x405140 */
{
    ObjTurret *pTurret;
    GxVec2 vIn;
    GxVec2 vCart;
    GxVec2 vSum;

    for (pTurret = (ObjTurret *)pNode->pTurretHead; pTurret != NULL;
         pTurret = pTurret->pNext) {                   /* +0x18 @0x405155 */
        if (pTurret->nTypeId == nTypeId) {             /* @0x405151 */
            break;
        }
    }
    if (pTurret == NULL) {                             /* @0x40514b */
        return 0;                                      /* EAX=0 @0x40515c */
    }
    gxVec2Set(&vIn, pTurret->vPolar.x, pNode->flScaleA + pTurret->vPolar.y);
    gxVec2FromPolar(&vCart, &vIn);                     /* @0x405181 */
    gxVec2Add(&vSum, &vCart, &pNode->vPos);            /* @0x405194 */
    return (int)vSum.y;                                /* ftol @0x4051ad */
}

/* objPolarPosLookup2 @0x4051c0 — same as objPolarPosLookup but returns the
 * world z component (the +0x20 slot of the sum). */
int objPolarPosLookup2(WorldNode *pNode, int nTypeId) /* @0x4051c0 */
{
    ObjTurret *pTurret;
    GxVec2 vIn;
    GxVec2 vCart;
    GxVec2 vSum;

    for (pTurret = (ObjTurret *)pNode->pTurretHead; pTurret != NULL;
         pTurret = pTurret->pNext) {                   /* @0x4051d5 */
        if (pTurret->nTypeId == nTypeId) {             /* @0x4051d1 */
            break;
        }
    }
    if (pTurret == NULL) {                             /* @0x4051cb */
        return 0;                                      /* @0x4051dc */
    }
    gxVec2Set(&vIn, pTurret->vPolar.x, pNode->flScaleA + pTurret->vPolar.y);
    gxVec2FromPolar(&vCart, &vIn);                     /* @0x405201 */
    gxVec2Add(&vSum, &vCart, &pNode->vPos);            /* @0x405214 */
    return (int)vSum.x;                                /* ftol @0x40522d */
}

/* objListFindFloat @0x405240 — find pNode's turret entry whose nTypeId
 * matches nTypeId and return its absolute heading scaled to the 15-bit
 * binary-angle unit: (int)((flScaleA + flAngle) * (65536/π) * 0.5). */
int objListFindFloat(WorldNode *pNode, int nTypeId) /* @0x405240 */
{
    static const float g_flRadToBin = 20860.455078125f; /* @0x44b2f0 (65536/π) */
    static const double g_dblHalf = 0.5;                /* @0x44b2e8 */
    ObjTurret *pTurret;

    for (pTurret = (ObjTurret *)pNode->pTurretHead; pTurret != NULL;
         pTurret = pTurret->pNext) {                   /* @0x40524f */
        if (pTurret->nTypeId == nTypeId) {             /* @0x40524b */
            return (int)((pNode->flScaleA + pTurret->flAngle) * g_flRadToBin *
                         g_dblHalf);                   /* @0x40526e */
        }
    }
    return 0;                                          /* XOR AX,AX @0x405256 */
}

/* objDistToPoint @0x405050 — Euclidean ground-plane distance from the
 * node's vPos {x=z, y=x} to {flA, flB} (components pair in vPos order). */
float objDistToPoint(WorldNode *pNode, float flA, float flB) /* @0x405050 */
{
    float flDx = flA - pNode->vPos.x; /* @0x405050 */
    float flDy = flB - pNode->vPos.y; /* @0x405057 */
    return sqrtf(flDx * flDx + flDy * flDy); /* @0x40505e..0x405068 */
}

/* nodeChannelAvgFloat @0x4050c0 — average flHeight over pNode's child-mesh
 * entries whose nChannelKey matches nChannelKey; 0.0f when there is none. */
float nodeChannelAvgFloat(WorldNode *pNode, int nChannelKey) /* @0x4050c0 */
{
    ObjChildMesh *pMesh;
    float flSum = 0.0f;    /* @0x4050c3 */
    float flCount = 0.0f;  /* @0x4050c9 */

    for (pMesh = (ObjChildMesh *)pNode->pChildMeshHead; pMesh != NULL;
         pMesh = pMesh->pNext) {                         /* @0x4050d3 */
        if (pMesh->nChannelKey == nChannelKey) {         /* @0x4050d7 */
            flSum += pMesh->flHeight;                    /* +0x40 @0x4050dc */
            flCount += g_flOne;                          /* @0x4050e1 */
        }
    }
    if (flCount == g_flZero) {                           /* @0x4050f2 */
        return g_flZero;                                 /* @0x405103 */
    }
    return flSum / flCount;                              /* FDIVRP @0x40510c */
}

/* objFindTurret @0x405120 — return the first child-mesh entry of pNode
 * whose nChannelKey matches nChannelKey (despite the name this walks the
 * +0x08 ObjChildMesh list, not the turret list), or NULL. */
ObjChildMesh *objFindTurret(WorldNode *pNode, int nChannelKey) /* @0x405120 */
{
    ObjChildMesh *pMesh;

    for (pMesh = (ObjChildMesh *)pNode->pChildMeshHead; pMesh != NULL;
         pMesh = pMesh->pNext) {                         /* @0x40512b */
        if (pMesh->nChannelKey == nChannelKey) {         /* @0x40512e */
            return pMesh;                                /* @0x405139 */
        }
    }
    return NULL;                                         /* @0x405137 */
}

/* --- player collision clusters (levelObjectsCartsCameraInit @0x411b70) --- */


/* objTurretAdd @0x405280 — allocate a 0x1c turret entry, zero both polar
 * vectors, store the polarized {nPosZ, nPosX} position twice, scale nAngle
 * to radians (nAngle*g_flPi*g_dblAngleScale, i.e. 15-bit binary angle) and
 * head-insert into pNode's +0x0c turret list. nTypeId is the caller's id
 * (levelObjectsCartsCameraInit passes the record's scene node). */
void objTurretAdd(WorldNode *pNode, int nPosX, int nPosY, int nPosZ,
                  short nAngle, int nTypeId) /* @0x405280 */
{
    ObjTurret *pTurret = (ObjTurret *)malloc(sizeof(ObjTurret)); /* operator_new @0x43dd42 */
    GxVec2 vIn;

    (void)nPosY;
    if (pTurret != NULL) {
        gxVec2SetAngleZero(&pTurret->vPolar);                /* @0x434f90 @0x4052ba */
        gxVec2SetAngleZero(&pTurret->vPos);                  /* @0x4052c2 */
    }
    gxVec2Set(&vIn, (float)nPosZ, (float)nPosX);             /* @0x434fa0 @0x4052f4 */
    pTurret->vPos = vIn;                                     /* @0x4052fc */
    mathVec2Polar(&pTurret->vPolar, &vIn);                   /* @0x435060 @0x405314 */
    pTurret->flAngle = (float)(int)nAngle * g_flPi * (float)g_dblAngleScale; /* @0x405338 */
    pTurret->nTypeId = nTypeId;                              /* @0x40533e */
    pTurret->pNext = (ObjTurret *)pNode->pTurretHead;        /* @0x405349 */
    pNode->pTurretHead = pTurret;                            /* @0x405353 */
}

/* objTurretSetValue @0x405370 — walk pNode's child-mesh list and write
 * nValue1/nValue2 into every entry whose nChannelKey matches nKey. */
void objTurretSetValue(WorldNode *pNode, int nKey, int nValue1, int nValue2) /* @0x405370 */
{
    ObjChildMesh *pMesh;

    for (pMesh = (ObjChildMesh *)pNode->pChildMeshHead; pMesh != NULL;
         pMesh = pMesh->pNext) {                             /* @0x40538f */
        if (pMesh->nChannelKey == nKey) {                    /* @0x405384 */
            pMesh->nValue1 = nValue1;                        /* @0x405389 */
            pMesh->nValue2 = nValue2;                        /* @0x40538c */
        }
    }
}

/* nodeAddChildMesh @0x4053a0 — allocate a 0x4c child-mesh entry, zero the
 * four polar vectors, store the polarized {nKeyZ, nX} position twice plus
 * its polar form, the raw extents/channel key/mesh tag, then head-insert
 * into pNode's +0x08 list. Finally the world coords are fromPolar of the
 * entry's polar position rotated by pNode->flScaleA, stored twice. */
void nodeAddChildMesh(WorldNode *pNode, int nX, int nY, int nKeyZ,
                      float flExtentA, float flExtentB, int nChannelKey,
                      float flExtentC, int nMeshId) /* @0x4053a0 */
{
    ObjChildMesh *pMesh = (ObjChildMesh *)malloc(sizeof(ObjChildMesh)); /* @0x43dd42 */
    GxVec2 vIn;
    GxVec2 vOut;

    (void)nY;
    if (pMesh != NULL) {
        gxVec2SetAngleZero(&pMesh->vPolar);                  /* @0x4053da */
        gxVec2SetAngleZero(&pMesh->vPos);                    /* @0x4053e2 */
        gxVec2SetAngleZero(&pMesh->vWorldA);                 /* @0x4053ea */
        gxVec2SetAngleZero(&pMesh->vWorldB);                 /* @0x4053f2 */
    }
    gxVec2Set(&vIn, (float)nKeyZ, (float)nX);                /* @0x405424 */
    pMesh->vPos = vIn;                                       /* @0x40542c */
    mathVec2Polar(&pMesh->vPolar, &vIn);                     /* @0x405444 */
    pMesh->flExtentA = flExtentA;                            /* +0x14 @0x405463 */
    pMesh->flExtentB = flExtentB;                            /* +0x10 @0x40546a */
    pMesh->flExtentC = flExtentC;                            /* +0x18 @0x40546d */
    pMesh->nChannelKey = nChannelKey;                        /* +0x0c @0x405476 */
    pMesh->flVertVel = 0.0f;                                 /* +0x44 @0x405479 */
    pMesh->nMeshId = nMeshId;                                /* +0x00 @0x40547c */
    pMesh->nValue1 = 0;                                      /* +0x04 @0x40547e */
    pMesh->nValue2 = 0;                                      /* +0x08 @0x405481 */
    pMesh->pNext = (ObjChildMesh *)pNode->pChildMeshHead;    /* @0x405488 */
    pNode->pChildMeshHead = pMesh;                           /* @0x40548b */
    gxVec2Set(&vIn, pMesh->vPolar.x, pMesh->vPolar.y + pNode->flScaleA); /* @0x40549f */
    gxVec2FromPolar(&vOut, &vIn);                            /* @0x434fc0 @0x4054a9 */
    pMesh->vWorldA = vOut;                                   /* @0x4054b1 */
    pMesh->vWorldB = vOut;                                   /* @0x4054bc */
}

/* nodeSetTransformFromChannels @0x404e00 — place pNode at {nPosZ, nPosX}
 * (vPos and the vPosB copy), store nUnk at +0x3c, rotate by nAngle (same
 * 15-bit scaling as objTurretAdd) into flScaleA/flScaleB, then recompute
 * every child mesh's world coords from its polar form and raycast the
 * floor with sceneRayFindNearest @0x42a750 (1000.0 search distance,
 * flExtentA as the wall radius). */
void nodeSetTransformFromChannels(WorldNode *pNode, int nPosX, int nPosY,
                                  int nPosZ, int nUnk, short nAngle) /* @0x404e00 */
{
    ObjChildMesh *pMesh;
    GxVec2 vIn;
    GxVec2 vOut;

    pNode->vPos.x = (float)nPosZ;                            /* +0x20 @0x404e18 */
    pNode->field_3c = nUnk;                                  /* +0x3c @0x404e22 */
    pNode->vPos.y = (float)nPosX;                            /* +0x24 @0x404e27 */
    pNode->vPosB = pNode->vPos;                              /* @0x404e34 */
    pNode->flScaleA = (float)(int)nAngle * g_flPi * (float)g_dblAngleScale; /* @0x404e46 */
    pNode->flScaleB = pNode->flScaleA;                       /* +0x34 @0x404e49 */
    for (pMesh = (ObjChildMesh *)pNode->pChildMeshHead; pMesh != NULL;
         pMesh = pMesh->pNext) {                             /* @0x404e85 */
        gxVec2Set(&vIn, pMesh->vPolar.x, pMesh->vPolar.y + pNode->flScaleA); /* @0x404e60 */
        gxVec2FromPolar(&vOut, &vIn);                        /* @0x404e6a */
        pMesh->vWorldA = vOut;                               /* @0x404e71 */
        pMesh->vWorldB = vOut;                               /* @0x404e7c */
    }
    for (pMesh = (ObjChildMesh *)pNode->pChildMeshHead; pMesh != NULL;
         pMesh = pMesh->pNext) {                             /* @0x404edc */
        pMesh->pSurface = sceneRayFindNearest(               /* @0x42a750 @0x404ebe */
            pMesh->vWorldA.x + pNode->vPos.x,
            pMesh->vWorldA.y + pNode->vPos.y,
            (float)nPosY, 1000.0f, pMesh->flExtentA);        /* @0x447a000=1000.0 @0x404eaa */
        pMesh->flHeight = (float)nPosY;                      /* +0x40 @0x404eca */
        pMesh->flVertVel = 0.0f;                             /* +0x44 @0x404ecd */
    }
}

/* sceneObjCtor3 @0x4146a0 — zero the 0x50-byte EventObject and set nId
 * (+0x08), origin (+0x38/+0x3c; the callers pass the world Z into flX and
 * the world X into flY, matching the {x=z, y=x} vPos convention) and both
 * vertical bounds (+0x40/+0x44 = flHeight). Used for the landed
 * thrown-item pickup objects (itemThrowUpdate) and the level-event
 * bonus items. */
void sceneObjCtor3(EventObject *pObj, int nId, float flX, float flY,
                   float flHeight) /* @0x4146a0 */
{
    pObj->field_00 = 0;
    pObj->pLineList = NULL;
    pObj->nId = nId;
    pObj->field_0c = 0;
    pObj->field_10 = 0;
    pObj->field_14 = 0;
    pObj->field_18 = 0;
    pObj->field_1c = 0;
    pObj->field_20 = 0;
    pObj->field_24 = 0;
    pObj->field_28 = 0;
    pObj->field_2c = 0;
    pObj->field_30 = 0;
    pObj->field_34 = 0;
    pObj->flOriginX = flX;
    pObj->flOriginZ = flY;
    pObj->flHeightA = flHeight;
    pObj->flHeightB = flHeight;
    pObj->pHashNext = NULL;
    pObj->pHashPrev = NULL;
}

static const float g_fl1_2e6 = 1.2e-06f;  /* @0x44b2f4 (bytes 57 5C 1A 36) */
static const float g_fl1_5e6 = 1.5e-06f;  /* @0x44b300 (bytes AB A5 43 36) */

/* objSetPos @0x404ef0 — move a world node on the ground plane: the old
 * vPos is copied to vPosB (+0x28/+0x2c) and vPos (+0x20/+0x24) becomes
 * (flX, flZ). Used by objUpdatePhysics/objUpdateFire to place a node on
 * its collision shot (two calls in objUpdatePhysics leave vPosB on the
 * shot's from-position and vPos on its to-position). */
void objSetPos(WorldNode *pNode, float flX, float flZ) /* @0x404ef0 */
{
    pNode->vPosB.x = pNode->vPos.x;                      /* +0x28 @0x404ef2 */
    pNode->vPosB.y = pNode->vPos.y;                      /* +0x2c @0x404ef5 */
    pNode->vPos.x = flX;                                 /* +0x20 @0x404ef8 */
    pNode->vPos.y = flZ;                                 /* +0x24 @0x404efb */
}

/* objShotListClear @0x405590 — release the node's shot list built by
 * objShotCollide (objShotCollide walk) / objCollideCheck: objShotListFree
 * the chain, free the head block, zero the list head (+0x10) and the shot
 * count (+0x14). */
void objShotListClear(WorldNode *pNode) /* @0x405590 */
{
    if (pNode->pShotList != NULL) {                      /* @0x405594 */
        objShotListFree(pNode->pShotList);               /* @0x406110 @0x40559b */
        memFreeDirect(pNode->pShotList);                 /* @0x4055a5 */
    }
    pNode->pShotList = NULL;                             /* +0x10 @0x4055ab */
    pNode->field_14 = 0;                                 /* +0x14 @0x4055af */
}

/* objUpdatePhysics @0x405680 — collision-response pass 1 (objUpdateAll
 * calls it twice per frame). Per enabled node, 40 times per frame
 * (@0x405dd9): objShotListClear, objShotCollide (rebuilds the shot list),
 * then apply the response. Single shot (field_14 == 1): objSetPos onto the
 * shot's from-position and then its to-position (vPosB = from, vPos = to),
 * flScaleC = shot->field_1c, flScaleB smoothed toward flScaleA by
 * flBlend/(field_18+flBlend), and flImpulse = (dir - from)·slope *
 * shot->flScale * 1.2e-06 (@0x44b2f4); impact sfx bank 1 idx
 * pSfxSrc[1] vol 65000 id 1 with nFlags 0x24. Multiple shots: the same
 * response using the shot with the lowest flBlend/(field_18+flBlend)
 * ratio (@0x405bce). The original's g_nMovieFrame 400..500 debug trace
 * blocks call the (empty) nopDebugStub logger and are omitted. */
void objUpdatePhysics(void) /* @0x405680 */
{
    WorldNode *pNode;
    ShotObj *pShot;
    ShotObj *pBest;
    void *pEmitter;
    int nIter;
    int nX;
    int nZ;

    for (pNode = g_pObjHead; pNode != NULL; pNode = pNode->pPrev) {  /* +0x00 walk @0x405de6 */
        if (pNode->field_1c == 0) {                      /* @0x4056b0 */
            continue;
        }
        for (nIter = 0; nIter < 0x28; nIter++) {         /* 40 substeps @0x405dd9 */
            objShotListClear(pNode);                     /* @0x405590 @0x4056c5 */
            objShotCollide(pNode);                       /* @0x4035e0 @0x4056cc */
            if (pNode->field_14 == 1 && pNode->pShotList != NULL) {  /* @0x405721 */
                pShot = (ShotObj *)pNode->pShotList;
                objSetPos(pNode, pShot->v0.x, pShot->v0.y);      /* @0x405894 */
                objSetPos(pNode, pShot->v2.x, pShot->v2.y);          /* @0x4058a6 */
                pNode->flScaleC = pShot->v1.y;                       /* +0x38 @0x4058c3 */
                pNode->flScaleB += (pNode->flScaleA - pNode->flScaleB) *
                                   pShot->v3.x /
                                   (pShot->v1.x + pShot->v3.x);       /* +0x34 @0x4058c9 */
                pNode->flImpulse =                                   /* +0x40 @0x4058ee */
                    ((pShot->v4.y - pShot->v0.y) * pShot->v5.y +
                     (pShot->v4.x - pShot->v0.x) * pShot->v5.x) *
                    pShot->flScale * g_fl1_2e6;                      /* @0x44b2f4 @0x4058f4 */
                pEmitter = malloc(0x1c);                             /* operator_new @0x43dd42 @0x4058fd */
                if (pEmitter != NULL) {                              /* @0x40590d */
                    nZ = (int)pNode->vPos.x;                         /* ftol +0x20 @0x40591a */
                    nX = (int)pNode->vPos.y;                         /* ftol +0x24 @0x405924 */
                    sndPlaySfx3D(pEmitter, 1,                        /* @0x42bcd0 @0x405941 */
                                 (unsigned int)pShot->pSrc[1], 65000, 1, 0, 0,
                                 nX, 0, nZ, 0x24);
                }
            } else if (pNode->field_14 > 1 && pNode->pShotList != NULL) {  /* @0x405a20 */
                pBest = (ShotObj *)pNode->pShotList;
                for (pShot = pBest->pNext; pShot != NULL; pShot = pShot->pNext) {  /* @0x405beb */
                    if (pShot->v3.x / (pShot->v1.x + pShot->v3.x) <
                        pBest->v3.x / (pBest->v1.x + pBest->v3.x)) {               /* @0x405bce */
                        pBest = pShot;
                    }
                }
                objSetPos(pNode, pBest->v0.x, pBest->v0.y);          /* @0x405c4b */
                objSetPos(pNode, pBest->v2.x, pBest->v2.y);          /* @0x405c5a */
                pNode->flScaleC = pBest->v1.y;                       /* +0x38 @0x405c6f */
                pNode->flScaleB += (pNode->flScaleA - pNode->flScaleB) *
                                   pBest->v3.x /
                                   (pBest->v1.x + pBest->v3.x);       /* +0x34 @0x405c78 */
                pNode->flImpulse =                                   /* +0x40 @0x405c9f */
                    ((pBest->v4.y - pBest->v0.y) * pBest->v5.y +
                     (pBest->v4.x - pBest->v0.x) * pBest->v5.x) *
                    pBest->flScale * g_fl1_2e6;                      /* @0x405ca5 */
                pEmitter = malloc(0x1c);                             /* @0x405cae */
                if (pEmitter != NULL) {                              /* @0x405cbc */
                    nZ = (int)pNode->vPos.x;                         /* ftol +0x20 @0x405ccd */
                    nX = (int)pNode->vPos.y;                         /* ftol +0x24 @0x405cd8 */
                    sndPlaySfx3D(pEmitter, 1,                        /* @0x42bcd0 @0x405cf4 */
                                 (unsigned int)pBest->pSrc[1], 65000, 1, 0, 0,
                                 nX, 0, nZ, 0x24);
                }
            }
            pNode->field_18++;                               /* +0x18 @0x405dcd */
        }
    }
}

/* objUpdateFire @0x405e10 — fire/hazard pass between the two
 * objUpdatePhysics runs. Pass 1 (@0x405e31): clear + rebuild each enabled
 * node's shot list with objCollideCheck @0x404ac0. Pass 2 (@0x405e5c):
 * apply the response — single shot: objSetPos onto the shot's to-position
 * (vPosB keeps the previous vPos), flScaleB = flScaleA (snap), field_3c =
 * shot->field_18, flScaleC = shot->field_1c, flImpulse = (dir - to)·slope *
 * flScale * 1.5e-06 (@0x44b300); fire sfx bank 1 idx pSfxSrc[2] vol 65000
 * id 0 with nFlags 0x24; then objWalkAnimSync(pParent, *(pAnimTarget+0x44))
 * (the victim node's pParent record) for the node's parent. Multiple shots: the same response with the shot
 * of maximum field_18 (@0x405f5a); the walk-anim sync always reads the
 * list head's pAnimTarget (@0x40600f). */
void objUpdateFire(void) /* @0x405e10 */
{
    WorldNode *pNode;
    ShotObj *pShot;
    ShotObj *pBest;
    void *pEmitter;
    int nX;
    int nZ;
    struct PlayerRecord *pRec;

    for (pNode = g_pObjHead; pNode != NULL; pNode = pNode->pPrev) {  /* @0x405e46 */
        if (pNode->field_1c != 0) {                      /* @0x405e31 */
            objShotListClear(pNode);                     /* @0x405590 @0x405e3a */
            objCollideCheck(pNode);                      /* @0x404ac0 @0x405e41 */
        }
    }
    for (pNode = g_pObjHead; pNode != NULL; pNode = pNode->pPrev) {  /* @0x405e46/0x406025 */
        if (pNode->field_1c == 0) {                      /* @0x405e5c */
            continue;
        }
        if (pNode->field_14 == 1 && pNode->pShotList != NULL) {  /* @0x405e67 */
            pShot = (ShotObj *)pNode->pShotList;
            objSetPos(pNode, pShot->v2.x, pShot->v2.y);            /* @0x405e7e */
            pNode->flScaleB = pNode->flScaleA;                     /* +0x34 @0x405e94 */
            pNode->field_3c = pShot->v1.x;                         /* +0x3c @0x405e9d */
            pNode->flScaleC = pShot->v1.y;                         /* +0x38 @0x405ea3 */
            pNode->flImpulse =                                     /* +0x40 @0x405eba */
                ((pShot->v4.y - pShot->v2.y) * pShot->v5.y +
                 (pShot->v4.x - pShot->v2.x) * pShot->v5.x) *
                pShot->flScale * g_fl1_5e6;                        /* @0x44b300 @0x405ec0 */
            pEmitter = malloc(0x1c);                               /* @0x405ec9 */
            if (pEmitter != NULL) {                                /* @0x405ed7 */
                nZ = (int)pNode->vPos.x;                           /* ftol +0x20 @0x405ee8 */
                nX = (int)pNode->vPos.y;                           /* ftol +0x24 @0x405ef3 */
                sndPlaySfx3D(pEmitter, 1,                          /* @0x42bcd0 @0x405f12 */
                             (unsigned int)pShot->pSrc[2], 65000, 0, 0, 0,
                             nX, 0, nZ, 0x24);
            }
            if (pNode->pParent != NULL) {                          /* +0x44 @0x405f17 */
                pRec = NULL;
                if (pShot->pAnimTarget != NULL) {                  /* +0x08 @0x405f2d */
                    pRec = *(struct PlayerRecord **)((char *)pShot->pAnimTarget + 0x44); /* @0x405f30 */
                }
                if (pRec != NULL) {                                /* @0x405f33 */
                    objWalkAnimSync((struct PlayerRecord *)pNode->pParent, pRec); /* @0x409b10 @0x40601c */
                }
            }
            pNode->field_18++;                                     /* +0x18 @0x406022 */
        } else if (pNode->field_14 > 1 && pNode->pShotList != NULL) {  /* @0x405f43 */
            pBest = (ShotObj *)pNode->pShotList;
            for (pShot = pBest->pNext; pShot != NULL; pShot = pShot->pNext) {  /* @0x405f54 */
                if (pBest->v1.x < pShot->v1.x) {                   /* @0x405f62 */
                    pBest = pShot;
                }
            }
            objSetPos(pNode, pBest->v2.x, pBest->v2.y);            /* @0x405f6f */
            pNode->flScaleB = pNode->flScaleA;                     /* +0x34 @0x405f81 */
            pNode->field_3c = pBest->v1.x;                         /* +0x3c @0x405f89 */
            pNode->flScaleC = pBest->v1.y;                         /* +0x38 @0x405f8f */
            pNode->flImpulse =                                     /* +0x40 @0x405fa6 */
                ((pBest->v4.x - pBest->v2.x) * pBest->v5.x +
                 (pBest->v4.y - pBest->v2.y) * pBest->v5.y) *
                pBest->flScale * g_fl1_5e6;                        /* @0x405fac */
            pEmitter = malloc(0x1c);                               /* @0x405fb5 */
            if (pEmitter != NULL) {                                /* @0x405fc3 */
                nZ = (int)pNode->vPos.x;                           /* @0x405fd4 */
                nX = (int)pNode->vPos.y;                           /* @0x405fdf */
                sndPlaySfx3D(pEmitter, 1,                          /* @0x42bcd0 @0x405ffb */
                             (unsigned int)pBest->pSrc[2], 65000, 0, 0, 0,
                             nX, 0, nZ, 0x24);
            }
            if (pNode->pParent != NULL) {                          /* @0x406000 */
                pRec = NULL;
                pShot = (ShotObj *)pNode->pShotList;               /* head @0x406012 */
                if (pShot->pAnimTarget != NULL) {                  /* +0x08 @0x406012 */
                    pRec = *(struct PlayerRecord **)((char *)pShot->pAnimTarget + 0x44); /* @0x406015 */
                }
                if (pRec != NULL) {                                /* @0x40601a */
                    objWalkAnimSync((struct PlayerRecord *)pNode->pParent, pRec); /* @0x409b10 @0x40601d */
                }
            }
            pNode->field_18++;                                     /* @0x406022 */
        }
    }
}

/* ===================================================================
 * shot-collision subsystem (objShotCtor @0x406050, objShotAdd @0x4054e0,
 * objShotListFree @0x406110, objSegCollideCollect @0x402ba0,
 * objShotCollide @0x4035e0, objCollideCheck @0x404ac0)
 * =================================================================== */

/* Shot-phase constants (read from the binary). */
static const float g_fl_0_2 = 0.2f;           /* @0x44b2d4 */
static const float g_flShotHalf = 0.5f;       /* @0x44b274 */
static const float g_fl_0_125 = 0.125f;       /* @0x44b2e0 */
static const float g_fl_0_333 = 0.33333334f;  /* @0x44b2dc */
static const float g_fl_0_25 = 0.25f;         /* @0x44b2d8 */
static const float g_fl_500 = 500.0f;         /* @0x44b2e4 */
static const double g_dbl_1 = -1.0;           /* @0x44b280 (perp-proj negate) */
static const double g_dbl_1_2 = 1.0;          /* @0x44b288 (phases 2-4 bounds) */

/* OBJ_HIT_IN_BOX — the ±flTol axis-aligned box test the original inlines
 * at 0x4038fd..0x403a54 (objShotCollide phase bounds) and 0x402e40..
 * (objSegCollideCollect): pHit must lie within
 * [min(pBoxA,pBoxB)-flTol, max(pBoxA,pBoxB)+flTol] on both components. */
#define OBJ_HIT_IN_BOX(pHit, pBoxA, pBoxB, flTol)                               \
    (((((pHit)->x <= (pBoxA)->x + (flTol)) &&                                   \
       ((pBoxB)->x - (flTol) <= (pHit)->x)) ||                                  \
      (((pHit)->x <= (pBoxB)->x + (flTol)) &&                                   \
       ((pBoxA)->x - (flTol) <= (pHit)->x))) &&                                 \
     ((((pHit)->y <= (pBoxA)->y + (flTol)) &&                                   \
       ((pBoxB)->y - (flTol) <= (pHit)->y)) ||                                  \
      (((pHit)->y <= (pBoxB)->y + (flTol)) &&                                   \
       ((pBoxA)->y - (flTol) <= (pHit)->y))))

/* OBJ_HIT_IN_BOX_XY — same box test with the box given as four scalar
 * components (the AiNavEdge V0/V1 pairs are separate fields). */
#define OBJ_HIT_IN_BOX_XY(pHit, flAx, flAy, flBx, flBy, flTol)                  \
    (((((pHit)->x <= (flAx) + (flTol)) &&                                       \
       ((flBx) - (flTol) <= (pHit)->x)) ||                                      \
      (((pHit)->x <= (flBx) + (flTol)) &&                                       \
       ((flAx) - (flTol) <= (pHit)->x))) &&                                     \
     ((((pHit)->y <= (flAy) + (flTol)) &&                                       \
       ((flBy) - (flTol) <= (pHit)->y)) ||                                      \
      (((pHit)->y <= (flBy) + (flTol)) &&                                       \
       ((flAy) - (flTol) <= (pHit)->y))))

/* objShotCtor @0x406050 — initialize a 0x44 ShotObj: six SetAngleZero
 * vec2s (+0x10..+0x38), then v3, v0, v2, v1, v4, v5, flScale, pSrc
 * (+0x0c), pAnimTarget (+0x08) and pNext/field_04 = 0 (store order per
 * the disassembly; v3 first). Original __thiscall RET 0x3c. */
ShotObj *objShotCtor(ShotObj *pShot, int *pSrc, float flV0x, float flV0y,
                     float flV2x, float flV2y, float flV1x, float flV1y,
                     float flV4x, float flV4y, float flV5x, float flV5y,
                     float flSpeed, float flV3x, float flV3y,
                     void *pAnimTarget) /* @0x406050 */
{
    gxVec2SetAngleZero(&pShot->v0);                    /* +0x10 @0x40605b */
    gxVec2SetAngleZero(&pShot->v1);                    /* +0x18 @0x406065 */
    gxVec2SetAngleZero(&pShot->v2);                    /* +0x20 @0x40606f */
    gxVec2SetAngleZero(&pShot->v3);                    /* +0x28 @0x406077 */
    gxVec2SetAngleZero(&pShot->v4);                    /* +0x30 @0x40607f */
    gxVec2SetAngleZero(&pShot->v5);                    /* +0x38 @0x406087 */
    pShot->v3.x = flV3x;                               /* +0x28 @0x406098 */
    pShot->v3.y = flV3y;                               /* +0x2c @0x40609f */
    pShot->v0.x = flV0x;                               /* +0x10 @0x4060a6 */
    pShot->v0.y = flV0y;                               /* +0x14 @0x4060ac */
    pShot->v2.x = flV2x;                               /* +0x20 @0x4060b3 */
    pShot->v2.y = flV2y;                               /* +0x24 @0x4060ba */
    pShot->v1.x = flV1x;                               /* +0x18 @0x4060c1 */
    pShot->v1.y = flV1y;                               /* +0x1c @0x4060c7 */
    pShot->v4.x = flV4x;                               /* +0x30 @0x4060ce */
    pShot->v4.y = flV4y;                               /* +0x34 @0x4060d5 */
    pShot->v5.x = flV5x;                               /* +0x38 @0x4060dc */
    pShot->v5.y = flV5y;                               /* +0x3c @0x4060e3 */
    pShot->flScale = flSpeed;                          /* +0x40 @0x4060ea */
    pShot->pSrc = pSrc;                                /* +0x0c @0x4060ed */
    pShot->pAnimTarget = pAnimTarget;                  /* +0x08 @0x4060f0 */
    pShot->pNext = NULL;                               /* +0x00 @0x4060f3 */
    pShot->field_04 = 0;                               /* +0x04 @0x4060f9 */
    return pShot;                                      /* @0x406100 */
}

/* objShotAdd @0x4054e0 — operator_new a 0x44 ShotObj (@0x43dd42) and
 * objShotCtor it with the same 15 args, then push onto pNode->pShotList
 * (+0x10) and bump the shot count (+0x14). The original dereferences the
 * allocation even when operator_new fails (NULL-deref on OOM); the
 * rebuild keeps the same shape. Original __thiscall RET 0x3c. */
void objShotAdd(WorldNode *pNode, int *pSrc, float flV0x, float flV0y,
                float flV2x, float flV2y, float flV1x, float flV1y,
                float flV4x, float flV4y, float flV5x, float flV5y,
                float flSpeed, float flV3x, float flV3y, void *pAnimTarget) /* @0x4054e0 */
{
    ShotObj *pShot;

    pShot = malloc(0x44);                              /* operator_new @0x43dd42 @0x4054fb */
    if (pShot != NULL) {                               /* @0x405507 */
        objShotCtor(pShot, pSrc, flV0x, flV0y, flV2x, flV2y, flV1x, flV1y,
                    flV4x, flV4y, flV5x, flV5y, flSpeed, flV3x, flV3y,
                    pAnimTarget);                      /* @0x406050 @0x405560 */
    }
    pShot->pNext = pNode->pShotList;                   /* +0x00 @0x405570 */
    pNode->pShotList = pShot;                          /* +0x10 @0x405572 */
    pNode->field_14++;                                 /* +0x14 @0x405575 */
}

/* objShotListFree @0x406110 — recursively free a shot list: recurse on
 * pNext, then memFreeDirect the next node. The head block itself is
 * freed by objShotListClear, which then zeroes the list head and count.
 * Original __fastcall. */
void objShotListFree(ShotObj *pShot) /* @0x406110 */
{
    ShotObj *pNext;

    pNext = pShot->pNext;                              /* +0x00 @0x406111 */
    if (pNext != NULL) {                               /* @0x406115 */
        objShotListFree(pNext);                        /* @0x406119 */
        memFreeDirect(pNext);                          /* @0x43dd37 @0x40611f */
    }
}

/* objSegCollideCollect @0x402ba0 — extend the surface array (NULL
 * terminated) with the pPeerMesh target of every connection edge
 * (+0x38 list) of the listed surfaces whose segment geometry touches
 * the moving A/B segment of pSrc, deduped against the existing entries:
 *  - near test: the edge V0 plane distance from A or B <= radius;
 *  - phase A/B: intersect the wall with a unit ray from A (then B)
 *    along the edge direction, ±1.0 box;
 *  - else/all-far: reflected-ray test with unit(-n)*radius extension
 *    (±1.0 on both boxes), then a perpendicular circle test
 *    (project A onto the edge normal, chord-shift the hit along A->B).
 * Original __thiscall. */
void objSegCollideCollect(WorldNode *pNode, ObjChildMesh *pSrc, void **apList) /* @0x402ba0 */
{
    GxVec2 vA;                 /* local_12c: vWorldA + vPos */
    GxVec2 vB;                 /* local_124: vWorldB + vPosB */
    GxVec2 vRay;               /* local_50/40: {flUnitX, flUnitNegZ} ray dir */
    GxVec2 vExt;               /* local_c8/d8: fromPolar({radius, angle(-n)}) */
    GxVec2 vPolar;             /* polar scratch local_a8/c0 */
    GxVec2 vS1;                /* edge/extended start local_11c/f8 */
    GxVec2 vS2;                /* edge/extended end local_f8 */
    GxVec2 vOutA;              /* phase-A intersection out local_104 */
    GxVec2 vOutB;              /* phase-B intersection out local_110 */
    GxVec2 vHit;               /* intersection local_134 */
    GxVec2 vPerpIn;            /* {1.0, angle+PI/2} local_f0 */
    GxVec2 vPerp;              /* local_b8 -> local_d0/cc */
    float flRad2;              /* local_114: radius^2 */
    float flProj;              /* local_e0: perp projection */
    int i;                     /* surface index */
    int j;                     /* dedup scan */
    AiNavEdge *pEdge;
    AiNavNode *pSurf;

    gxVec2SetAngleZero(&vA);                               /* local_12c @0x402c52 */
    gxVec2SetAngleZero(&vB);                               /* local_124 @0x402c58 */
    gxVec2SetAngleZero(&vOutA);                            /* local_104 @0x402c5e */
    gxVec2SetAngleZero(&vOutB);                            /* local_110 @0x402c63 */
    gxVec2SetAngleZero(&vPolar);                           /* local_b0 @0x402c68 */
    gxVec2SetAngleZero(&vPerpIn);                          /* local_f0 @0x402c6d */
    gxVec2SetAngleZero(&vHit);                             /* local_134 @0x402c72 */
    gxVec2SetAngleZero(&vExt);                             /* local_d8 @0x402c77 */
    gxVec2SetAngleZero(&vExt);                             /* local_c8 @0x402c7c */
    gxVec2SetAngleZero(&vPerp);                            /* local_d0 @0x402c81 */
    gxVec2SetAngleZero(&vS1);                              /* local_11c @0x402c86 */
    gxVec2SetAngleZero(&vS2);                              /* local_f8 @0x402c8a */
    flRad2 = pSrc->flExtentA * pSrc->flExtentA;            /* @0x402ba5 */
    gxVec2Add(&vA, &pSrc->vWorldA, &pNode->vPos);          /* @0x402c08 */
    gxVec2Add(&vB, &pSrc->vWorldB, &pNode->vPosB);         /* @0x402c28 */
    for (i = 0; apList[i] != NULL; i++) {                  /* @0x402c4c */
        pSurf = (AiNavNode *)apList[i];
        for (pEdge = pSurf->pConnList; pEdge != NULL;      /* @0x402c68 */
             pEdge = pEdge->pNext) {
            if (((pEdge->flV0Z - vA.x) * pEdge->flUnitX +
                 (pEdge->flV0X - vA.y) * pEdge->flUnitNegZ <=
                 pSrc->flExtentA) ||                       /* @0x402c92 */
                ((pEdge->flV0Z - vB.x) * pEdge->flUnitX +
                 (pEdge->flV0X - vB.y) * pEdge->flUnitNegZ <=
                 pSrc->flExtentA)) {                       /* @0x402cdc */
                /* Phase A — unit-direction ray from A. */
                gxVec2Set(&vRay, pEdge->flUnitX, pEdge->flUnitNegZ); /* @0x402cfb */
                gxVec2Add(&vS2, &vA, &vRay);               /* local_90 @0x402d0d */
                mathSegIntersect(pEdge->flV0Z, pEdge->flV0X,          /* @0x406130 @0x402d4c */
                                 pEdge->flV1Z, pEdge->flV1X,
                                 vA.x, vA.y, vS2.x, vS2.y, &vOutA.x);
                if (OBJ_HIT_IN_BOX_XY(&vOutA, pEdge->flV0Z, pEdge->flV0X, /* @0x402d8c */
                                      pEdge->flV1Z, pEdge->flV1X,
                                      (float)g_dbl_1_2)) {
                    goto objSegAppend;                     /* @0x403570 */
                }
                /* Phase B — same ray from B. */
                gxVec2Set(&vRay, pEdge->flUnitX, pEdge->flUnitNegZ); /* @0x402e10 */
                gxVec2Add(&vS2, &vB, &vRay);               /* local_70 @0x402e22 */
                mathSegIntersect(pEdge->flV0Z, pEdge->flV0X,          /* @0x402e61 */
                                 pEdge->flV1Z, pEdge->flV1X,
                                 vB.x, vB.y, vS2.x, vS2.y, &vOutB.x);
                if (OBJ_HIT_IN_BOX_XY(&vOutB, pEdge->flV0Z, pEdge->flV0X, /* @0x402ec0 */
                                      pEdge->flV1Z, pEdge->flV1X,
                                      (float)g_dbl_1_2) ||
                    !(((pNode->vPos.x - pEdge->flV0Z) *
                       (pNode->vPos.x - pEdge->flV0Z) +
                       (pNode->vPos.y - pEdge->flV0X) *
                       (pNode->vPos.y - pEdge->flV0X) > flRad2) && /* @0x40301a */
                      ((pNode->vPos.x - pEdge->flV1Z) *
                       (pNode->vPos.x - pEdge->flV1Z) +
                       (pNode->vPos.y - pEdge->flV1X) *
                       (pNode->vPos.y - pEdge->flV1X) > flRad2) && /* @0x40304a */
                      ((pNode->vPosB.x - pEdge->flV0Z) *
                       (pNode->vPosB.x - pEdge->flV0Z) +
                       (pNode->vPosB.y - pEdge->flV0X) *
                       (pNode->vPosB.y - pEdge->flV0X) > flRad2) && /* @0x40307a */
                      ((pNode->vPosB.x - pEdge->flV1Z) *
                       (pNode->vPosB.x - pEdge->flV1Z) +
                       (pNode->vPosB.y - pEdge->flV1X) *
                       (pNode->vPosB.y - pEdge->flV1X) > flRad2))) { /* @0x4030aa */
                    goto objSegAppend;                 /* @0x403570 */
                }
                /* fall through to the far-side phase C tests */
            }
            /* Phase C1 — reflected-ray test from the A side. */
            if ((vA.x - vB.x) * pEdge->flUnitX +                   /* @0x4030c0 */
                (vA.y - vB.y) * pEdge->flUnitNegZ >= 0.0f &&
                (pEdge->flV0Z - vA.x) * pEdge->flUnitX +           /* @0x4030e6 */
                (pEdge->flV0X - vA.y) * pEdge->flUnitNegZ <=
                pSrc->flExtentA) {
                gxVec2Set(&vRay, -pEdge->flUnitX, -pEdge->flUnitNegZ); /* @0x403100 */
                mathVec2Polar(&vPolar, &vRay);                     /* @0x435060 @0x403105 */
                gxVec2Set(&vExt, pSrc->flExtentA, vPolar.y);       /* local_d8 */
                gxVec2FromPolar(&vExt, &vExt);                     /* @0x434fc0 @0x403129 */
                gxVec2Set(&vS1, pEdge->flV0Z, pEdge->flV0X);
                gxVec2Add(&vS1, &vS1, &vExt);                      /* local_11c @0x40313b */
                gxVec2Set(&vS2, pEdge->flV1Z, pEdge->flV1X);
                gxVec2Add(&vS2, &vS2, &vExt);                      /* local_f8 @0x40315f */
                mathSegIntersect(vA.x, vA.y, vB.x, vB.y,           /* @0x406130 @0x403182 */
                                 vS1.x, vS1.y, vS2.x, vS2.y, &vHit.x);
                if (OBJ_HIT_IN_BOX(&vHit, &vA, &vB, (float)g_dbl_1_2) && /* @0x4031ae */
                    OBJ_HIT_IN_BOX(&vHit, &vS1, &vS2, (float)g_dbl_1_2)) {
                    goto objSegAppend;                 /* @0x403570 */
                }
            }
            /* Phase C2 — perpendicular circle test. */
            gxVec2Set(&vRay, vB.x - vA.x, vB.y - vA.y);            /* local_58 @0x4031d6 */
            mathVec2Polar(&vPolar, &vRay);                         /* local_c0 @0x4031a4 */
            gxVec2Set(&vPerpIn, 1.0f, vPolar.y + g_flHalfPi);      /* local_f0 @0x4031c2 */
            gxVec2FromPolar(&vPerp, &vPerpIn);                     /* local_b8 @0x4031d2 */
            flProj = (vA.x - pEdge->flV0Z) * vPerp.x +             /* local_e0 @0x4031e6 */
                     (vA.y - pEdge->flV0X) * vPerp.y;
            if (flProj < 0.0f) {                                   /* @0x403206 */
                flProj = (float)((double)flProj * g_dbl_1);        /* @0x44b280 @0x403212 */
            }
            if (flProj <= pSrc->flExtentA) {                       /* @0x403224 */
                gxVec2Set(&vS1, pEdge->flV0Z, pEdge->flV0X);       /* local_48 @0x403232 */
                gxVec2Set(&vS2, pEdge->flV0Z - vPerp.x,            /* local_38 @0x40324c */
                          pEdge->flV0X - vPerp.y);
                mathSegIntersect(vA.x, vA.y, vB.x, vB.y,           /* @0x406130 @0x403270 */
                                 vS1.x, vS1.y, vS2.x, vS2.y, &vHit.x);
                gxVec2Set(&vExt, sqrtf(flRad2 - flProj * flProj),  /* local_28 @0x4033f4 */
                          vPolar.y);
                gxVec2FromPolar(&vExt, &vExt);                     /* local_18 @0x403414 */
                gxVec2Add(&vHit, &vHit, &vExt);                    /* local_8 @0x403420 */
                if (OBJ_HIT_IN_BOX(&vHit, &vA, &vB, (float)g_dbl_1_2)) { /* @0x403430 */
objSegAppend:
                    for (j = 0; apList[j] != NULL; j++) {          /* @0x403578 */
                        if (apList[j] == (void *)pEdge->pPeerMesh) { /* @0x4035a2 */
                            break;                     /* dedup skip @0x4035aa */
                        }
                    }
                    if (apList[j] == NULL) {
                        apList[j] = (void *)pEdge->pPeerMesh;      /* @0x403585 */
                        apList[j + 1] = NULL;                      /* @0x40358b */
                    }
                }
            }
        }
    }
}

/* objShotCollide @0x4035e0 — rebuild pNode's shot list (+0x10) for the
 * objUpdatePhysics response. Per enabled +0x08 sub-object: world
 * positions A (vWorldA + vPos) and B (vWorldB + vPosB) must differ;
 * raycast the surfaces (sceneRayFindNearest 500.0, pSurface cache),
 * extend the list via objSegCollideCollect, then four hit phases over
 * the surfaces' pEdgeList (+0x3c) and pConnList (+0x38) edges, each
 * objShotAdd'ing a record with:
 *   v0 = hit - vWorldA, v2 = (hit + refl) - vWorldA,
 *   v1 = {dA*k, reflected angle} (k = 0.2 phases 1/2, 0.5 phases 3/4),
 *   v4 = hit, v5 = unit(V1-V0), flScale = dA*k,
 *   v3 = polar(hit - B) = {dB, angle}, pAnimTarget = NULL.
 * Phases 1/3 test the walk edges, 2/4 the connection walls (adding the
 * pPeerMesh plane test at the wall probe / edge V0 point). Phases 1/2
 * intersect the extended edge, 3/4 the inward-shifted edge with a
 * perpendicular-circle chord adjustment (g_dbl_1 negate @0x44b280).
 * Finally sceneRayFindSorted re-sorts the array and refreshes the
 * pSurface cache. Original __fastcall. */
void objShotCollide(WorldNode *pNode) /* @0x4035e0 */
{
    ObjChildMesh *pSrc;
    void *apSurfaces[20];      /* local_230..local_1e0 (0x50 bytes) */
    GxVec2 vA;                 /* local_31c: shooter A world pos */
    GxVec2 vB;                 /* local_314: shooter B world pos */
    GxVec2 vRay;               /* -n / line-dir polar-in scratch */
    GxVec2 vPolar;             /* polar scratch local_238/278/1e0/1d8 */
    GxVec2 vPolarAB;           /* polar(B - A) local_2a8 (persistent) */
    GxVec2 vPolarExt;          /* {radius, angle(-n)} local_2ec */
    GxVec2 vExt;               /* unit(-n) * radius local_2d4 */
    GxVec2 vS1;                /* edge start (extended) local_30c */
    GxVec2 vS2;                /* edge end (extended) local_300 */
    GxVec2 vHit;               /* intersection local_324 */
    GxVec2 vRefl;              /* reflected dir local_180 */
    GxVec2 vV4;                /* hit + refl local_240 */
    GxVec2 vPolarB;            /* polar(hit - B) local_258 */
    GxVec2 vPolarA;            /* polar(hit - A), then reflected local_2c4 */
    GxVec2 vPolarDir;          /* polar(V1 - V0) local_288 */
    GxVec2 vDirIn;             /* {1.0, angle + PI/2} local_2b4 */
    GxVec2 vPerp;              /* perp unit local_2bc */
    GxVec2 vProbe;             /* hit + extend local_90 (phase 2) */
    GxVec2 vTgt;               /* wall probe target local_30c (phase 2) */
    GxVec2 vOff;               /* chord offset local_178 (phases 3/4) */
    float flRad2;              /* radius^2 local_2ac */
    float flProj;              /* perp projection local_304 */
    float flPlanePeer;         /* wall target plane value + flExtentC */
    float flPlaneSurf;         /* cached surface plane value */
    GxVec2 vV0;                /* shot v0: gxVec2Sub(hit, vWorldA) */
    GxVec2 vV2;                /* shot v2: gxVec2Sub(v4, vWorldA) */
    int i;
    AiNavEdge *pEdge;
    AiNavNode *pPeer;

    gxVec2SetAngleZero(&vA);                               /* local_31c @0x40361e */
    gxVec2SetAngleZero(&vB);                               /* local_314 @0x403624 */
    gxVec2SetAngleZero(&vHit);                             /* local_324 @0x40362a */
    gxVec2SetAngleZero(&vPolarExt);                        /* local_2ec @0x403630 */
    gxVec2SetAngleZero(&vExt);                             /* local_2d4 @0x403636 */
    gxVec2SetAngleZero(&vS1);                              /* local_30c @0x40363c */
    gxVec2SetAngleZero(&vS2);                              /* local_300 @0x403640 */
    for (pSrc = (ObjChildMesh *)pNode->pChildMeshHead; pSrc != NULL;
         pSrc = pSrc->pNext) {                                 /* @0x403644 */
        if (pSrc->nMeshId == 0) {                              /* @0x40365a */
            continue;
        }
        gxVec2Add(&vA, &pSrc->vWorldA, &pNode->vPos);          /* @0x40368c */
        gxVec2Add(&vB, &pSrc->vWorldB, &pNode->vPosB);         /* @0x4036a4 */
        if (vA.x == vB.x && vA.y == vB.y) {                    /* @0x4036c0 */
            continue;
        }
        if (pSrc->pSurface == NULL) {                          /* @0x4036e0 */
            pSrc->pSurface = sceneRayFindNearest(vA.x, vA.y,   /* @0x42a750 @0x40370b */
                                                 pSrc->flExtentB, 500.0f,
                                                 pSrc->flExtentA);
            if (pSrc->pSurface == NULL) {                      /* @0x403730 */
                pSrc->pSurface = sceneRayFindNearest(vB.x, vB.y, /* @0x40374e */
                                                     pSrc->flExtentB, 500.0f,
                                                     pSrc->flExtentA);
                if (pSrc->pSurface == NULL) {                  /* @0x40377a */
                    continue;      /* next sub-object: no phases, no sorted pass */
                }
                apSurfaces[1] = NULL;                          /* @0x403785 */
                apSurfaces[0] = pSrc->pSurface;
            } else {
                apSurfaces[0] = pSrc->pSurface;                /* @0x40379d */
                apSurfaces[1] = sceneRayFindNearest(vB.x, vB.y, /* @0x4037a8 */
                                                    pSrc->flExtentB, 500.0f,
                                                    pSrc->flExtentA);
                apSurfaces[2] = NULL;
            }
        } else {
            apSurfaces[0] = pSrc->pSurface;                    /* @0x4037c8 */
            apSurfaces[1] = NULL;
        }
        objSegCollideCollect(pNode, pSrc, apSurfaces);         /* @0x402ba0 @0x4037e0 */

        /* Phase 1 — walk edges (+0x3c), segment-intersect, scale 0.2. */
        for (i = 0; apSurfaces[i] != NULL; i++) {              /* @0x40380a */
            for (pEdge = ((AiNavNode *)apSurfaces[i])->pEdgeList;
                 pEdge != NULL; pEdge = pEdge->pNext) {        /* @0x403828 */
                if ((vA.y - vB.y) * pEdge->flUnitNegZ +        /* @0x403860 */
                    (vA.x - vB.x) * pEdge->flUnitX >= 0.0f &&
                    (pEdge->flV0Z - vA.x) * pEdge->flUnitX +   /* @0x40388a */
                    (pEdge->flV0X - vA.y) * pEdge->flUnitNegZ <=
                    pSrc->flExtentA) {
                    gxVec2Set(&vRay, -pEdge->flUnitX, -pEdge->flUnitNegZ); /* @0x4038ba */
                    mathVec2Polar(&vPolar, &vRay);             /* @0x435060 @0x4038c4 */
                    gxVec2Set(&vPolarExt, pSrc->flExtentA, vPolar.y); /* local_2ec */
                    gxVec2FromPolar(&vExt, &vPolarExt);        /* local_2d4 @0x4038e6 */
                    gxVec2Set(&vS1, pEdge->flV0Z, pEdge->flV0X);
                    gxVec2Add(&vS1, &vS1, &vExt);              /* @0x403908 */
                    gxVec2Set(&vS2, pEdge->flV1Z, pEdge->flV1X);
                    gxVec2Add(&vS2, &vS2, &vExt);              /* @0x403930 */
                    mathSegIntersect(vA.x, vA.y, vB.x, vB.y,   /* @0x406130 @0x403952 */
                                     vS1.x, vS1.y, vS2.x, vS2.y, &vHit.x);
                    if (OBJ_HIT_IN_BOX(&vHit, &vA, &vB, g_flOne) &&  /* @0x4038fd */
                        OBJ_HIT_IN_BOX(&vHit, &vS1, &vS2, g_flOne)) {
                        gxVec2Set(&vPolarB, vHit.x - vB.x, vHit.y - vB.y); /* local_190 @0x40398a */
                        mathVec2Polar(&vPolarB, &vPolarB);     /* @0x40399c */
                        gxVec2Set(&vPolarA, vHit.x - vA.x, vHit.y - vA.y); /* local_20 @0x4039b4 */
                        mathVec2Polar(&vPolarA, &vPolarA);     /* @0x4039c6 */
                        vPolarA.y = vPolarA.y -                /* reflect @0x4039dc */
                            ((vPolarA.y - vPolarExt.y) + (vPolarA.y - vPolarExt.y));
                        vPolarA.x = vPolarA.x * g_fl_0_2;      /* @0x44b2d4 @0x4039ec */
                        gxVec2FromPolar(&vRefl, &vPolarA);     /* local_180 @0x4039f4 */
                        gxVec2Add(&vV4, &vHit, &vRefl);        /* local_240 @0x403a06 */
                        gxVec2Set(&vPolarDir, pEdge->flV1Z - pEdge->flV0Z, /* local_170 @0x403a18 */
                                  pEdge->flV1X - pEdge->flV0X);
                        mathVec2Polar(&vPolarDir, &vPolarDir); /* @0x403a3e */
                        vPolarDir.x = 1.0f;                    /* @0x403a34 */
                        gxVec2FromPolar(&vExt, &vPolarDir);    /* V5 @0x403af4 */
                        objShotAdd(pNode, (int *)pSrc,                /* @0x4054e0 @0x403bde */
                                   vHit.x - pSrc->vWorldA.x,   /* v0 @0x403bcf */
                                   vHit.y - pSrc->vWorldA.y,
                                   vV4.x - pSrc->vWorldA.x,    /* v2 @0x403bb2 */
                                   vV4.y - pSrc->vWorldA.y,
                                   vPolarA.x, vPolarA.y,       /* v1 @0x403af4 */
                                   vHit.x, vHit.y,             /* v4 @0x403b17 */
                                   vExt.x, vExt.y,             /* v5 @0x403b7d */
                                   vPolarA.x,                  /* flScale @0x403af0 */
                                   vPolarB.x, vPolarB.y,       /* v3 @0x403b91 */
                                   NULL);
                    }
                }
            }
        }

        /* Phase 2 — connection walls (+0x38), scale 0.2, plus the
         * pPeerMesh plane test at the wall probe point. */
        for (i = 0; apSurfaces[i] != NULL; i++) {
            for (pEdge = ((AiNavNode *)apSurfaces[i])->pConnList;
                 pEdge != NULL; pEdge = pEdge->pNext) {        /* @0x403c40 */
                if (pEdge->pPeerMesh == NULL) {                /* @0x403c31 */
                    continue;
                }
                if ((vA.y - vB.y) * pEdge->flUnitNegZ +
                    (vA.x - vB.x) * pEdge->flUnitX < 0.0f ||
                    (pEdge->flV0Z - vA.x) * pEdge->flUnitX +
                    (pEdge->flV0X - vA.y) * pEdge->flUnitNegZ >
                    pSrc->flExtentA) {                         /* @0x403c52 */
                    continue;
                }
                gxVec2Set(&vRay, -pEdge->flUnitX, -pEdge->flUnitNegZ); /* @0x403c56 */
                mathVec2Polar(&vPolar, &vRay);                 /* @0x403c60 */
                gxVec2Set(&vPolarExt, pSrc->flExtentA, vPolar.y);
                gxVec2FromPolar(&vExt, &vPolarExt);            /* @0x403c78 */
                gxVec2Set(&vS1, pEdge->flV0Z, pEdge->flV0X);
                gxVec2Add(&vS1, &vS1, &vExt);                  /* @0x403c9a */
                gxVec2Set(&vS2, pEdge->flV1Z, pEdge->flV1X);
                gxVec2Add(&vS2, &vS2, &vExt);                  /* @0x403cbc */
                mathSegIntersect(vA.x, vA.y, vB.x, vB.y,       /* @0x403f80 */
                                 vS1.x, vS1.y, vS2.x, vS2.y, &vHit.x);
                if (OBJ_HIT_IN_BOX(&vHit, &vA, &vB, (float)g_dbl_1_2) && /* @0x403d92 */
                    OBJ_HIT_IN_BOX(&vHit, &vS1, &vS2, (float)g_dbl_1_2)) {
                    gxVec2Add(&vProbe, &vHit, &vExt);          /* local_90 @0x403fd8? */
                    mathSegIntersect(vHit.x, vHit.y, vProbe.x, vProbe.y, /* @0x403f80 */
                                     pEdge->flV0Z, pEdge->flV0X,
                                     pEdge->flV1Z, pEdge->flV1X, &vTgt.x);
                    pPeer = (AiNavNode *)pEdge->pPeerMesh;     /* @0x403f93 */
                    if (pSrc->pSurface != NULL) {              /* @0x403f85 */
                        flPlanePeer =                          /* @0x403f96 */
                            (vTgt.x - (float)pPeer->nRefZ) * pPeer->flSlopeZ +
                            (vTgt.y - (float)pPeer->nRefX) * pPeer->flSlopeX +
                            (float)pPeer->nRefY + pSrc->flExtentC;
                        flPlaneSurf =                          /* @0x403fb2 */
                            (vTgt.x - (float)((AiNavNode *)pSrc->pSurface)->nRefZ) *
                            ((AiNavNode *)pSrc->pSurface)->flSlopeZ +
                            (vTgt.y - (float)((AiNavNode *)pSrc->pSurface)->nRefX) *
                            ((AiNavNode *)pSrc->pSurface)->flSlopeX +
                            (float)((AiNavNode *)pSrc->pSurface)->nRefY;
                        if (flPlanePeer < flPlaneSurf &&       /* @0x403fcb */
                            flPlanePeer < pSrc->flHeight) {    /* @0x403ff4 */
                            gxVec2Set(&vPolarB, vHit.x - vB.x, vHit.y - vB.y);
                            mathVec2Polar(&vPolarB, &vPolarB); /* @0x404030 */
                            gxVec2Set(&vPolarA, vHit.x - vA.x, vHit.y - vA.y);
                            mathVec2Polar(&vPolarA, &vPolarA); /* @0x40405c */
                            vPolarA.y = vPolarA.y -            /* @0x404077 */
                                ((vPolarA.y - vPolarExt.y) + (vPolarA.y - vPolarExt.y));
                            vPolarA.x = vPolarA.x * g_fl_0_2;  /* @0x404083 */
                            gxVec2FromPolar(&vRefl, &vPolarA); /* @0x40408d */
                            gxVec2Add(&vV4, &vHit, &vRefl);    /* @0x4040a0 */
                            gxVec2Set(&vPolarDir, pEdge->flV1Z - pEdge->flV0Z,
                                      pEdge->flV1X - pEdge->flV0X);
                            mathVec2Polar(&vPolarDir, &vPolarDir); /* @0x4040e2 */
                            vPolarDir.x = 1.0f;                /* @0x404108 */
                            gxVec2FromPolar(&vExt, &vPolarDir); /* v5 @0x404116 */
                            objShotAdd(pNode, (int *)pSrc,            /* @0x4054e0 @0x40416f */
                                       vHit.x - pSrc->vWorldA.x,
                                       vHit.y - pSrc->vWorldA.y,
                                       vV4.x - pSrc->vWorldA.x, vV4.y - pSrc->vWorldA.y,
                                       vPolarA.x, vPolarA.y,
                                       vHit.x, vHit.y,
                                       vExt.x, vExt.y,
                                       vPolarA.x,
                                       vPolarB.x, vPolarB.y,
                                       NULL);
                        }
                    }
                }
            }
        }

        /* Perpendicular setup for the circle phases. */
        gxVec2Set(&vPolarDir, vB.x - vA.x, vB.y - vA.y);       /* local_1b8 @0x4041f4 */
        mathVec2Polar(&vPolarAB, &vPolarDir);                  /* local_2a8 @0x4041fe */
        gxVec2Set(&vDirIn, 1.0f, vPolarAB.y + g_flHalfPi);     /* local_2b4 @0x4041ea */
        gxVec2FromPolar(&vPerp, &vDirIn);                      /* local_2bc @0x40421a */
        flRad2 = pSrc->flExtentA * pSrc->flExtentA;            /* local_2ac @0x40421c */

        /* Phase 3 — walk edges, perpendicular-circle test, scale 0.5. */
        for (i = 0; apSurfaces[i] != NULL; i++) {
            for (pEdge = ((AiNavNode *)apSurfaces[i])->pEdgeList;
                 pEdge != NULL; pEdge = pEdge->pNext) {        /* @0x40423a */
                flProj = (vA.x - pEdge->flV0Z) * vPerp.x +     /* @0x40425c */
                         (vA.y - pEdge->flV0X) * vPerp.y;
                if (flProj < 0.0f) {                           /* @0x404272 */
                    flProj = (float)((double)flProj * g_dbl_1); /* @0x44b280 @0x404282 */
                }
                if (flProj <= pSrc->flExtentA) {               /* @0x404290 */
                    gxVec2Set(&vS1, pEdge->flV0Z, pEdge->flV0X); /* local_1a8 @0x40428c */
                    gxVec2Set(&vS2, pEdge->flV0Z - vPerp.x,    /* local_198 @0x404246 */
                              pEdge->flV0X - vPerp.y);
                    mathSegIntersect(vA.x, vA.y, vB.x, vB.y,   /* @0x406130 @0x4042b0 */
                                     vS1.x, vS1.y, vS2.x, vS2.y, &vHit.x);
                    gxVec2Set(&vOff, sqrtf(flRad2 - flProj * flProj), /* local_188 @0x4042f0 */
                              vPolarAB.y);
                    gxVec2FromPolar(&vOff, &vOff);             /* local_178 @0x404304 */
                    gxVec2Add(&vHit, &vHit, &vOff);            /* local_168 @0x404310 */
                    if (OBJ_HIT_IN_BOX(&vHit, &vA, &vB, (float)g_dbl_1_2)) { /* @0x404336 */
                        gxVec2Set(&vPolar, pEdge->flV0Z - vHit.x, /* local_158 @0x40435a */
                                  pEdge->flV0X - vHit.y);
                        mathVec2Polar(&vPolar, &vPolar);       /* local_1e0 @0x404372 */
                        gxVec2Set(&vPolarB, vHit.x - vB.x, vHit.y - vB.y); /* local_148 @0x404386 */
                        mathVec2Polar(&vPolarB, &vPolarB);     /* local_298 @0x40439a */
                        gxVec2Set(&vPolarA, vHit.x - vA.x, vHit.y - vA.y); /* local_138 @0x4043ae */
                        mathVec2Polar(&vPolarA, &vPolarA);     /* local_2dc @0x4043c0 */
                        vPolarA.y = vPolarA.y -                /* @0x4043e2 */
                            ((vPolarA.y - vPolar.y) + (vPolarA.y - vPolar.y));
                        vPolarA.x = vPolarA.x * g_flShotHalf;  /* @0x4043f2 */
                        gxVec2FromPolar(&vRefl, &vPolarA);     /* local_128 @0x4043fa */
                        gxVec2Add(&vV4, &vHit, &vRefl);        /* local_280 @0x40440c */
                        gxVec2Set(&vPolarDir, pEdge->flV1Z - pEdge->flV0Z, /* local_108 @0x40441e */
                                  pEdge->flV1X - pEdge->flV0X);
                        mathVec2Polar(&vPolarDir, &vPolarDir); /* local_290 @0x40444e */
                        vPolarDir.x = 1.0f;
                        gxVec2FromPolar(&vExt, &vPolarDir);    /* v5 @0x40445c */
                        gxVec2Sub(&vV0, &vHit, &pSrc->vWorldA); /* @0x4045d7 */
                        gxVec2Sub(&vV2, &vV4, &pSrc->vWorldA); /* @0x4045a2 */
                        objShotAdd(pNode, (int *)pSrc,                /* @0x4054e0 @0x4045e3 */
                                   vV0.x, vV0.y,
                                   vV2.x, vV2.y,
                                   vPolarA.x, vPolarA.y,
                                   vHit.x, vHit.y,
                                   vExt.x, vExt.y,
                                   vPolarA.x,
                                   vPolarB.x, vPolarB.y,
                                   NULL);
                    }
                }
            }
        }

        /* Phase 4 — connection walls, perpendicular-circle test, scale
         * 0.5, plus the pPeerMesh plane test at the edge V0 point. */
        for (i = 0; apSurfaces[i] != NULL; i++) {
            for (pEdge = ((AiNavNode *)apSurfaces[i])->pConnList;
                 pEdge != NULL; pEdge = pEdge->pNext) {        /* @0x40463c */
                if (pEdge->pPeerMesh == NULL) {                /* @0x404650 */
                    continue;
                }
                flProj = (vA.x - pEdge->flV0Z) * vPerp.x +     /* @0x404658 */
                         (vA.y - pEdge->flV0X) * vPerp.y;
                if (flProj < 0.0f) {                           /* @0x40468a */
                    flProj = (float)((double)flProj * g_dbl_1);
                }
                if (flProj > pSrc->flExtentA) {                /* @0x404696 */
                    continue;
                }
                gxVec2Set(&vS1, pEdge->flV0Z, pEdge->flV0X);   /* local_d8 @0x4046a0 */
                gxVec2Set(&vS2, pEdge->flV0Z - vPerp.x,        /* local_c8 @0x4046ba */
                          pEdge->flV0X - vPerp.y);
                mathSegIntersect(vA.x, vA.y, vB.x, vB.y,       /* @0x406130 @0x4046de */
                                 vS1.x, vS1.y, vS2.x, vS2.y, &vHit.x);
                gxVec2Set(&vOff, sqrtf(flRad2 - flProj * flProj), /* local_b8 @0x404700 */
                          vPolarAB.y);
                gxVec2FromPolar(&vOff, &vOff);                 /* local_a8 @0x404748 */
                gxVec2Add(&vHit, &vHit, &vOff);                /* local_98 @0x404754 */
                if (OBJ_HIT_IN_BOX(&vHit, &vA, &vB, (float)g_dbl_1_2) && /* @0x40477a */
                    pSrc->pSurface != NULL) {                  /* @0x4047a6 */
                    pPeer = (AiNavNode *)pEdge->pPeerMesh;
                    flPlanePeer =                              /* @0x4047b4 */
                        (vS1.x - (float)pPeer->nRefZ) * pPeer->flSlopeZ +
                        (vS1.y - (float)pPeer->nRefX) * pPeer->flSlopeX +
                        (float)pPeer->nRefY + pSrc->flExtentC;
                    flPlaneSurf =
                        (vS1.x - (float)((AiNavNode *)pSrc->pSurface)->nRefZ) *
                        ((AiNavNode *)pSrc->pSurface)->flSlopeZ +
                        (vS1.y - (float)((AiNavNode *)pSrc->pSurface)->nRefX) *
                        ((AiNavNode *)pSrc->pSurface)->flSlopeX +
                        (float)((AiNavNode *)pSrc->pSurface)->nRefY;
                    if (flPlanePeer < flPlaneSurf &&           /* @0x4047f2 */
                        flPlanePeer < pSrc->flHeight) {        /* @0x40481c */
                        gxVec2Set(&vPolar, pEdge->flV0Z - vHit.x, /* local_88 @0x404856 */
                                  pEdge->flV0X - vHit.y);
                        mathVec2Polar(&vPolar, &vPolar);       /* local_1d8 @0x40486e */
                        gxVec2Set(&vPolarB, vHit.x - vB.x, vHit.y - vB.y); /* local_78 @0x404882 */
                        mathVec2Polar(&vPolarB, &vPolarB);     /* local_270 @0x40489c */
                        gxVec2Set(&vPolarA, vHit.x - vA.x, vHit.y - vA.y); /* local_68 @0x4048b0 */
                        mathVec2Polar(&vPolarA, &vPolarA);     /* local_2cc @0x4048d6 */
                        vPolarA.y = vPolarA.y -                /* @0x404906 */
                            ((vPolarA.y - vPolar.y) + (vPolarA.y - vPolar.y));
                        vPolarA.x = vPolarA.x * g_flShotHalf;  /* @0x40491a */
                        gxVec2FromPolar(&vRefl, &vPolarA);     /* local_58 @0x404922 */
                        gxVec2Add(&vV4, &vHit, &vRefl);        /* local_250 @0x404934 */
                        gxVec2Set(&vPolarDir, pEdge->flV1Z - pEdge->flV0Z, /* local_38 @0x404946 */
                                  pEdge->flV1X - pEdge->flV0X);
                        mathVec2Polar(&vPolarDir, &vPolarDir); /* local_260 @0x40497a */
                        vPolarDir.x = 1.0f;
                        gxVec2FromPolar(&vExt, &vPolarDir);    /* v5 @0x404988 */
                        gxVec2Sub(&vV0, &vHit, &pSrc->vWorldA); /* @0x404a41 */
                        gxVec2Sub(&vV2, &vV4, &pSrc->vWorldA); /* @0x404a0e */
                        objShotAdd(pNode, (int *)pSrc,                /* @0x4054e0 @0x404a4d */
                                   vV0.x, vV0.y,
                                   vV2.x, vV2.y,
                                   vPolarA.x, vPolarA.y,
                                   vHit.x, vHit.y,
                                   vExt.x, vExt.y,
                                   vPolarA.x,
                                   vPolarB.x, vPolarB.y,
                                   NULL);
                    }
                }
            }
        }
        pSrc->pSurface = sceneRayFindSorted(vA.x, vA.y,        /* @0x42a7c0 @0x404a80 */
                                            pSrc->flExtentB, 500.0f,
                                            pSrc->flExtentA, apSurfaces);
    }
}

/* objCollideCheck @0x404ac0 — the objUpdateFire counterpart of
 * objShotCollide: per enabled +0x08 sub-object, walk the g_pObjHead
 * nodes (skipping pNode itself and disabled ones) and their +0x08
 * sub-objects, and objShotAdd a record whenever the other sub-object's
 * world circle overlaps pSrc's (radius = flExtentA + flExtentA, height
 * band ±500 @0x44b2e4):
 *   mid = B + unit(A - other) * (r - dist) / 2,
 *   ratio = (flExtentB * 0.5) / other->flExtentB,
 *   v1 = polarSum(polarSum({(r - dist) / ratio * 0.125, angle(A - other)},
 *                          {ratio * pNode->field_3c * 0.5, pNode->flScaleC}),
 *                 {pW->field_3c / ratio * 0.333, pW->flScaleC}),
 *   v5 = fromPolar({1.0, pW->flScaleC - PI/2}), flScale = pW->field_3c * 0.25,
 *   v0 = B - vWorldA, v2 = mid - vWorldA, v4 = B, v3 = v1,
 *   pAnimTarget = pW (its +0x44 pParent feeds objWalkAnimSync).
 * Original __fastcall. */
void objCollideCheck(WorldNode *pNode) /* @0x404ac0 */
{
    ObjChildMesh *pSrc;
    ObjChildMesh *pOther;
    WorldNode *pW;
    GxVec2 vA;                 /* local_84: vWorldA + vPos */
    GxVec2 vB;                 /* local_8c: vWorldB + vPosB */
    GxVec2 vC;                 /* local_94: other vWorldA + pW->vPos */
    GxVec2 vDir;               /* A - C polar-in local_40 */
    GxVec2 vPolar;             /* {dist, angle} local_a8 */
    GxVec2 vExt;               /* fromPolar({rem * 0.5, angle}) local_20 */
    GxVec2 vMid;               /* B + vExt local_78 */
    GxVec2 vT1;                /* first polar sum local_38 */
    GxVec2 vT2;                /* second polar sum local_28 -> v1 */
    GxVec2 vS1;                /* {pW-ratio, pW-angle} local_58 */
    GxVec2 vS2;                /* {pNode-ratio, pNode-angle} local_48 */
    GxVec2 vUnit;              /* fromPolar({1.0, pW->flImpulse - PI/2}) local_70 */
    float flRadius;            /* local_10: r = flExtentA + flExtentA */
    float flDist2;             /* dx*dx + dy*dy */
    float flDist;              /* local_7c: sqrt */
    float flRem;               /* r - dist */
    GxVec2 vV0;                /* shot v0: gxVec2Sub(B, vWorldA) */
    GxVec2 vV2;                /* shot v2: gxVec2Sub(mid, vWorldA) */
    float flRatio;             /* (flExtentB * 0.5) / other->flExtentB */
    float flSpeed;             /* pW->field_3c * 0.25 */

    gxVec2SetAngleZero(&vPolar);                           /* local_a0 @0x404acc */
    gxVec2SetAngleZero(&vA);                               /* local_84 @0x404ad5 */
    gxVec2SetAngleZero(&vB);                               /* local_8c @0x404ade */
    gxVec2SetAngleZero(&vC);                               /* local_94 @0x404ae7 */
    gxVec2SetAngleZero(&vUnit);                            /* local_68 @0x404af0 */
    for (pSrc = (ObjChildMesh *)pNode->pChildMeshHead; pSrc != NULL;
         pSrc = pSrc->pNext) {                                 /* @0x404af9 */
        if (pSrc->nMeshId == 0) {                              /* @0x404b04 */
            continue;
        }
        gxVec2Add(&vA, &pSrc->vWorldA, &pNode->vPos);          /* local_84 @0x404b1d */
        gxVec2Add(&vB, &pSrc->vWorldB, &pNode->vPosB);         /* local_8c @0x404b3f */
        for (pW = g_pObjHead; pW != NULL; pW = pW->pPrev) {    /* @0x404b46 */
            if (pW == pNode || pW->field_1c == 0) {            /* @0x404b62 */
                continue;
            }
            for (pOther = (ObjChildMesh *)pW->pChildMeshHead; pOther != NULL;
                 pOther = pOther->pNext) {                     /* @0x404b75 */
                if (pSrc->flExtentB > pOther->flHeight + g_fl_500 || /* @0x404b80 */
                    pOther->flHeight - g_fl_500 > pSrc->flExtentB) {
                    continue;
                }
                gxVec2Add(&vC, &pOther->vWorldA, &pW->vPos);   /* local_94 @0x404bbe */
                flRadius = pOther->flExtentA + pSrc->flExtentA; /* @0x404bd3 */
                flDist2 = (vA.x - vC.x) * (vA.x - vC.x) +
                          (vA.y - vC.y) * (vA.y - vC.y);       /* @0x404bf5 */
                if (flDist2 > flRadius * flRadius) {           /* @0x404c03 */
                    continue;
                }
                flDist = sqrtf(flDist2);                       /* local_7c @0x404c1a */
                gxVec2Set(&vDir, vA.x - vC.x, vA.y - vC.y);    /* local_40 @0x404c31 */
                mathVec2Polar(&vPolar, &vDir);                 /* local_a8 @0x404c3b */
                flRem = flRadius - flDist;                     /* @0x404c40 */
                gxVec2Set(&vExt, flRem * g_flShotHalf, vPolar.y); /* @0x404c64 */
                gxVec2FromPolar(&vExt, &vExt);                 /* local_20 @0x404c72 */
                gxVec2Add(&vMid, &vB, &vExt);                  /* local_78 @0x404c82 */
                flRatio = (pSrc->flExtentB * g_flShotHalf) /
                          pOther->flExtentB;                   /* @0x404c87 */
                gxVec2Set(&vT1, (flRem / flRatio) * g_fl_0_125, /* local_a8 @0x404cb0 */
                          vPolar.y);
                gxVec2Set(&vS1, (pW->field_3c / flRatio) * g_fl_0_333, /* local_58 @0x404cd2 */
                          pW->flScaleC);
                gxVec2Set(&vS2, flRatio * pNode->field_3c * g_flShotHalf, /* local_48 @0x404cf4 */
                          pNode->flScaleC);
                gxVec2RotateAdd(&vT1, &vT1, &vS2);             /* local_38 @0x404d07 */
                gxVec2RotateAdd(&vT2, &vT1, &vS1);             /* local_28 @0x404d18 */
                gxVec2Set(&vUnit, 1.0f, pW->flScaleC - g_flHalfPi); /* local_70 @0x404d3b */
                gxVec2FromPolar(&vUnit, &vUnit);               /* v5 @0x404d69 */
                flSpeed = pW->field_3c * g_fl_0_25;            /* +0x3c @0x404d4f */
                gxVec2Sub(&vV0, &vB, &pSrc->vWorldA);          /* @0x435020 @0x404db3 */
                gxVec2Sub(&vV2, &vMid, &pSrc->vWorldA);        /* @0x404d8a */
                objShotAdd(pNode, (int *)pSrc,                        /* @0x4054e0 @0x404dc5 */
                           vV0.x, vV0.y,
                           vV2.x, vV2.y,
                           vT2.x, vT2.y,                       /* v1 = v3 @0x404d2b */
                           vB.x, vB.y,                         /* v4 @0x404d6e */
                           vUnit.x, vUnit.y,                   /* v5 @0x404d69 */
                           flSpeed,                            /* @0x404d52 */
                           vT2.x, vT2.y,                       /* v3 @0x404d47 */
                           pW);
            }
        }
    }
}
