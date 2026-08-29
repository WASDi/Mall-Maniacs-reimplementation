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

/* objFindById @0x414a90 — bucket = nId % 0xff; walk the +0x48 chain,
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
        pNode->_pad40 = 0;    /* +0x40 @0x4055fc */
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
