#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "mstring.h"
#include "pool.h"

/* =====================================================================
 * mstring.c — MString cluster (maniac.exe 0x4350xx/0x4356xx).
 * Buffers come from operator_new @0x43dd42 and are released with
 * memFreeDirect @0x43dd37 (CRT new/free glue — not reimplemented; the
 * rebuild maps them to malloc/free via memFreeDirect in pool.c).
 * ===================================================================== */

/* mStringCtorEmpty @0x435150 — allocate a 1-byte buffer holding "". */
void *mStringCtorEmpty(MString *pThis) /* @0x435150 */
{
    pThis->pPsz = (char *)malloc(1);
    pThis->pPsz[0] = '\0';
    pThis->nLen = 1;
    return pThis;
}

/* mStringCtorWithCapacity @0x435170 — allocate nCapacity+1 bytes, NUL at
 * [nCapacity], store nCapacity+1 as the allocation size. Used by
 * mStringSubstr / configNodeGetKey. */
void *mStringCtorWithCapacity(MString *pThis, int nCapacity) /* @0x435170 */
{
    pThis->pPsz = (char *)malloc((size_t)nCapacity + 1);
    pThis->pPsz[nCapacity] = '\0';
    pThis->nLen = nCapacity + 1;
    return pThis;
}

/* mStringCtorFromCStr @0x435100 — copy pPsz into an exact-fit buffer
 * (strlen+1 bytes, allocation size stored in nLen). */
void *mStringCtorFromCStr(MString *pThis, const char *pPsz) /* @0x435100 */
{
    size_t n = strlen(pPsz) + 1;
    pThis->nLen = (int)n;
    pThis->pPsz = (char *)malloc(n);
    memcpy(pThis->pPsz, pPsz, n);
    return pThis;
}

/* mStringFree @0x435430 — release the buffer only (original does not
 * reset the pointer). */
void mStringFree(MString *pThis) /* @0x435430 */
{
    memFreeDirect(pThis->pPsz);
}

/* mStringAssign @0x4353e0 — replace the buffer with a copy of pSrc's
 * (allocation size taken from pSrc->nLen). The original overwrites the
 * pointer without freeing the old buffer; kept as-is. */
void *mStringAssign(MString *pThis, MString *pSrc) /* @0x4353e0 */
{
    size_t n = strlen(pSrc->pPsz) + 1;
    pThis->nLen = pSrc->nLen;
    pThis->pPsz = (char *)malloc(n);
    memcpy(pThis->pPsz, pSrc->pPsz, n);
    return pThis;
}

/* mStringAssignCopy @0x435440 — mStringAssign with self-check and a
 * memFreeDirect of the previous buffer. */
void *mStringAssignCopy(MString *pThis, MString *pSrc) /* @0x435440 */
{
    size_t n;
    if (pThis == pSrc) return pThis;
    memFreeDirect(pThis->pPsz);
    n = strlen(pSrc->pPsz) + 1;
    pThis->nLen = pSrc->nLen;
    pThis->pPsz = (char *)malloc(n);
    memcpy(pThis->pPsz, pSrc->pPsz, n);
    return pThis;
}

/* mStringCStr @0x4351d0 — return the buffer. */
char *mStringCStr(MString *pThis) /* @0x4351d0 */
{
    return pThis->pPsz;
}

/* mStringEquals @0x435570 — lexicographic compare; only the equality
 * result is used by callers (returns 1 iff the strings are equal). */
int mStringEquals(MString *pThis, const char *pPsz) /* @0x435570 */
{
    return strcmp(pThis->pPsz, pPsz) == 0;
}

/* mStringEqualsMString @0x435510 — equality between two MStrings
 * (compares the buffers byte-wise). */
int mStringEqualsMString(MString *pThis, MString *pOther) /* @0x435510 */
{
    return strcmp(pThis->pPsz, pOther->pPsz) == 0;
}

/* mStringNotEquals @0x4355c0 — negated mStringEquals. */
int mStringNotEquals(MString *pThis, const char *pPsz) /* @0x4355c0 */
{
    return strcmp(pThis->pPsz, pPsz) != 0;
}

/* mStringAppendChar @0x4352b0 — grow by one byte and append. */
void mStringAppendChar(MString *pThis, char c) /* @0x4352b0 */
{
    int nLen = pThis->nLen;
    char *pNew = (char *)malloc((size_t)nLen + 1);
    memcpy(pNew, pThis->pPsz, (size_t)nLen);
    pNew[nLen - 1] = c;
    pNew[nLen] = '\0';
    memFreeDirect(pThis->pPsz);
    pThis->pPsz = pNew;
    pThis->nLen = nLen + 1;
}

/* mStringLength @0x435630 — strlen of the buffer (not nLen). */
int mStringLength(MString *pThis) /* @0x435630 */
{
    return (int)strlen(pThis->pPsz);
}

/* mStringCharAt @0x4351a0 — buffer[nIndex], '\0' when out of range. */
char mStringCharAt(MString *pThis, int nIndex) /* @0x4351a0 */
{
    size_t n = strlen(pThis->pPsz);
    if (nIndex < 0 || (size_t)nIndex >= n) return '\0';
    return pThis->pPsz[nIndex];
}

/* mStringSubstr @0x435200 — copy characters [nStart..nEnd] (inclusive)
 * into pOut via a temporary exact-fit MString. */
void *mStringSubstr(MString *pThis, MString *pOut, int nStart, int nEnd) /* @0x435200 */
{
    MString tmp;
    int i, nLen;
    mStringCtorWithCapacity(&tmp, mStringLength(pThis));
    nLen = 0;
    for (i = nStart; i <= nEnd; i++) {
        tmp.pPsz[nLen] = pThis->pPsz[i];
        nLen++;
    }
    tmp.pPsz[nLen] = '\0';
    mStringAssign(pOut, &tmp);
    mStringFree(&tmp);
    return pOut;
}

/* mStringToInt @0x4351e0 — fmtAtoi @0x43e75c (CRT atoi wrapper). */
int mStringToInt(MString *pThis) /* @0x4351e0 */
{
    return atoi(pThis->pPsz);
}

/* mStringToFloat @0x4351f0 — crtAtof (CRT atof). */
double mStringToFloat(MString *pThis) /* @0x4351f0 */
{
    return atof(pThis->pPsz);
}
