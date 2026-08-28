#ifndef MSTRING_H
#define MSTRING_H

/* mstring.h — MString {char *pPsz; int nLen} string class used by the config
 * subsystem and level loading. nLen is the allocated buffer size (including
 * the NUL), matching the original ctor/assign semantics; the string itself is
 * always NUL-terminated. */

typedef struct MString {
    char *pPsz;   /* +0x00 heap buffer (operator_new) */
    int   nLen;   /* +0x04 allocation size incl. NUL */
} MString;        /* 0x08 */

void   *mStringCtorEmpty(MString *pThis);                          /* @0x435150 */
void   *mStringCtorWithCapacity(MString *pThis, int nCapacity);    /* @0x435170 */
void   *mStringCtorFromCStr(MString *pThis, const char *pPsz);     /* @0x435100 */
void    mStringFree(MString *pThis);                               /* @0x435430 */
void   *mStringAssign(MString *pThis, MString *pSrc);              /* @0x4353e0 */
void   *mStringAssignCopy(MString *pThis, MString *pSrc);          /* @0x435440 */
char   *mStringCStr(MString *pThis);                               /* @0x4351d0 */
int     mStringEquals(MString *pThis, const char *pPsz);           /* @0x435570 */
int     mStringEqualsMString(MString *pThis, MString *pOther);     /* @0x435510 */
int     mStringNotEquals(MString *pThis, const char *pPsz);        /* @0x4355c0 */
void    mStringAppendChar(MString *pThis, char c);                 /* @0x4352b0 */
int     mStringLength(MString *pThis);                             /* @0x435630 */
char    mStringCharAt(MString *pThis, int nIndex);                 /* @0x4351a0 */
void   *mStringSubstr(MString *pThis, MString *pOut, int nStart, int nEnd); /* @0x435200 */
int     mStringToInt(MString *pThis);                              /* @0x4351e0 */
double  mStringToFloat(MString *pThis);                            /* @0x4351f0 */

#endif /* MSTRING_H */
