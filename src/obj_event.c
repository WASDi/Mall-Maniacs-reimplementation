#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "obj_event.h"
#include "gx.h"
#include "scene.h"
#include "config.h"
#include "util.h"
#include "pool.h"
#include "time.h"
#include "zone.h"
#include "player_physics.h"
#include "stubs.h"

/* =====================================================================
 * obj_event.c — EventObject registry: creation (.eo load), id-hash and
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

/* Note: the original pickup-mesh reads at 0x4583b8 + id*0x2c
 * (playerAiGrabItem @0x40eb89, playerGrabCart @0x40e9eb) alias
 * g_apLevelItemSlots[id-1].nMeshId @0x4583e4+(id-1)*0x2c — the
 * SceneObjTypeDef* written by levelSetup from items[%d]/mesh. There is no
 * separate grabb-mesh table. */

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

/* objGetPos @0x40f1b0 — objFindById lookup; writes the EventObject origin
 * (+0x38/+0x3c, the {vPos.x, vPos.y} axes) to pOutXZ and the floored
 * +0x40 height to *pOutHeight. Returns the EventObject or NULL. Used by
 * playerAiUpdate (mode 4), aiStateSetTargetItem and aiStateGrabObject. */
EventObject *objGetPos(int nId, int nOccurrence, float *pOutXZ, int *pOutHeight) /* @0x40f1b0 */
{
    EventObject *pObj = objFindById(nId, nOccurrence); /* @0x414a90 @0x40f1bb */

    if (pObj != NULL) {
        pOutXZ[0] = pObj->flOriginX;                   /* +0x38 @0x40f1c9 */
        pOutXZ[1] = pObj->flOriginZ;                   /* +0x3c @0x40f1d2 */
        *pOutHeight = (int)pObj->flHeightA;            /* ftol +0x40 @0x40f1db */
        return pObj;                                   /* @0x40f1e6 */
    }
    return NULL;                                       /* @0x40f1ea */
}

/* objGetCheckoutPos @0x40f6e0 — objFindById("goal", 0) and write +0x38/+0x3c
 * (the checkout EventObject position, {z, x} axes) to pOutPos; 0,0 when the
 * checkout object is missing. Used by aiStateReturnHome. */
void objGetCheckoutPos(float *pOutPos) /* @0x40f6e0 */
{
    EventObject *pObj = objFindById(0x6C6F6767, 0);    /* *(int*)"goal" @0x44f4e4 @0x40f6e5 */

    if (pObj != NULL) {
        pOutPos[0] = pObj->flOriginX;                  /* +0x38 @0x40f6ea */
        pOutPos[1] = pObj->flOriginZ;                  /* +0x3c @0x40f6ef */
        return;
    }
    pOutPos[0] = 0.0f;
    pOutPos[1] = 0.0f;
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
        ConfigNode *pFields;
        int nId;
        int i;

        fmtSprintf(szId, "%-4.4s", mStringCStr(&pNode->key));
        if (szId[0] == '_') {
            nId = fmtAtoi(szId + 1);                 /* @0x43e75c */
        } else {
            fmtSprintf(szId, "%4.4s", mStringCStr(&pNode->key));
            nId = *(int *)szId;                      /* id = first dword of the name */
        }
        /* [VERIFIED vs 0x406e93] The skip tests the COMPUTED id: '_' entries
         * go through fmtAtoi first (e.g. "_31" -> 0x1f). Testing the raw name
         * dword instead let the .eo burger zone register as a pickable id-0x1f
         * EventObject whose field_14 holds a config value — the nearest-target
         * scan then preferred it over the director's real burger and the grab
         * crashed dereferencing that value as a SceneNode. */
        if (nId == 0x1f) {
            nopDebugStub();                          /* "Burger EventObject found and ignored." @0x44e838 */
            continue;
        }

        pFields = configNodeGetId(pNode);               /* @0x406e59 */
        pEvent = (EventObject *)malloc(0x50);        /* operator_new @0x43dd42 */
        if (pEvent != NULL) {
            float flH2 = (float)configEnvGetDouble2(&env, pFields, "h2");
            float flH  = (float)configEnvGetDouble2(&env, pFields, "h");
            float flY  = (float)configEnvGetDouble2(&env, pFields, "y");
            float flX  = (float)configEnvGetDouble2(&env, pFields, "x");
            sceneObjCtor4(pEvent, nId, flX, flY, flH, flH2);   /* @0x414700 */
        }

        for (i = 1; ; i++) {
            ConfigNode *pLineNode;
            float flY2, flX2, flY1, flX1;
            float flOrgX = (pEvent != NULL) ? pEvent->flOriginX : 0.0f;
            float flOrgY = (pEvent != NULL) ? pEvent->flOriginZ : 0.0f;

            fmtSprintf(szKey, "line[%d]", i);        /* s_line__d @0x44e81c */
            pLineNode = configEnvGetValue(&env, pFields, szKey); /* @0x436560 */
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
            ConfigNode *pValue = configEnvGetValue(&env, pFields, "values");
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
