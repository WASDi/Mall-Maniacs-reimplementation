#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "pool.h"
#include "quest.h"
#include "time.h"
#include "custom_helpers.h"

QuestRecord *g_pQuestHead;  /* @0x458978 */
QuestRecord *g_pQuestTail;  /* @0x45897c */

/* questRecordCtor @0x40fec0 — build a quest record: category (1..3 -> 0..2)
 * to +0x0c, pool-copied question (+0x10), answer bool (1=N, 2=J -> +0x14),
 * then prepend into the g_pQuestHead/g_pQuestTail doubly linked list
 * (pNext +0x04, pPrev +0x08; an empty list also becomes the tail). */
QuestRecord *questRecordCtor(QuestRecord *pRec, int nCategory,
                             const char *pszQuestion, int bAnswer)
{
    QuestRecord *pTail;

    pRec->bAnswer = bAnswer;                                 /* +0x14 @0x40fed0 */
    pRec->nCategory = nCategory;                             /* +0x0c @0x40fed8 */
    pRec->pszQuestion = (char *)memPoolAllocZero(0, strlen(pszQuestion) + 1); /* @0x40fef0 */
    strcpy(pRec->pszQuestion, pszQuestion);                  /* inline rep move @0x40ff22 */
    pTail = pRec;
    if (g_pQuestHead != NULL) {
        g_pQuestHead->pPrev = pRec;                          /* @0x40ff74 */
        pTail = g_pQuestTail;
    }
    g_pQuestTail = pTail;                                    /* @0x40ff7d */
    pRec->pNext = g_pQuestHead;                              /* @0x40ff82 */
    g_pQuestHead = pRec;                                     /* @0x40ff87 */
    pRec->pPrev = NULL;                                      /* @0x40ff8c */
    return pRec;                                             /* @0x40ff93 */
}

/* questRecordDtor @0x40ff50 — free the question string and unlink the
 * record from the list (fixing head/tail and neighbour links). */
void questRecordDtor(QuestRecord *pRec)
{
    memPoolFree(0, pRec->pszQuestion);                       /* @0x40ff50 */
    if (g_pQuestHead == pRec) {                              /* @0x40ff6a */
        g_pQuestHead = pRec->pNext;
    }
    if (g_pQuestTail == pRec) {                              /* @0x40ff72 */
        g_pQuestTail = pRec->pPrev;
    }
    if (pRec->pNext != NULL) {                               /* @0x40ff79 */
        pRec->pNext->pPrev = pRec->pPrev;
    }
    if (pRec->pPrev != NULL) {                               /* @0x40ff81 */
        pRec->pPrev->pNext = pRec->pNext;
    }
}

/* questLoad @0x40ffa0 — parse quest.txt (XOR ^0x55 obfuscated, lines
 * "1|2|3 <question>? J|N") into quest records. The whole file is read,
 * deobfuscated in place and then walked line by line (CR stripped, LF
 * terminates, 255-char cap); an empty line ends the parse. The question
 * text is line[2] up to and including the first '?' (scan capped at
 * offset 0xfd) and the answer char two bytes after the '?' decides
 * bAnswer (J/j -> 2, N/n -> 1, anything else drops the line). */
void questLoad(const char *pszPath)
{
    FILE *fp;
    int nSize = 0;
    char *pBuf;
    char *pCur;
    char szLine[256];
    char szQuestion[256];
    int nCount = 0;
    int nCategory;
    int nPos;
    int bAnswer;
    QuestRecord *pRec;
    int i;

    fp = fopen(pszPath, "rb");                    /* fileOpen @0x43e68a, "rb" @0x44e6c0 @0x40ffd4 */
    if (fp != NULL) {
        fseek(fp, 0, SEEK_END);                   /* fileSeek @0x43e5a0 @0x40ffe7 */
        nSize = ftell(fp);                        /* fileTell @0x43e41d @0x40ffed */
        fseek(fp, 0, SEEK_SET);                   /* @0x40fff9 */
        pBuf = (char *)malloc(nSize);             /* operator_new @0x43dd42 @0x40ffff */
        fread(pBuf, 1, nSize, fp);                /* fileRead @0x43e306 @0x41000f */
        fclose(fp);                               /* fileClose @0x43e289 @0x410015 */
        for (i = 0; i < nSize; i++) {             /* XOR deobfuscation @0x410023 */
            pBuf[i] ^= 0x55;
        }
    } else {
        pBuf = (char *)pszPath;                   /* original quirk @0x410033 */
    }
    pCur = pBuf;
    for (;;) {
        int nLen = 0;

        do {                                      /* line copy @0x41003b */
            char c = *pCur;
            if (c == '\0') break;
            if (c == '\n') { pCur++; break; }
            if (c != '\r') szLine[nLen++] = c;
            pCur++;
        } while (nLen < 0xff);
        szLine[nLen] = '\0';                      /* @0x41005b */
        if (nLen == 0) {                          /* end of file @0x410062 */
            nopDebugStub();                       /* "Questions read: %d" ch0 @0x41019b */
            appLog("[quest] questions read: %d", nCount);
            free(pBuf);                           /* memFreeDirect @0x43dd37 @0x4101a5 */
            return;
        }
        if (szLine[0] == '\n' || szLine[0] == '\r') continue; /* @0x41006c (unreachable) */
        if (szLine[0] == '1') {                   /* @0x410074 */
            nCategory = 0;
        } else if (szLine[0] == '2') {            /* @0x41007c */
            nCategory = 1;
        } else if (szLine[0] == '3') {            /* @0x410087 */
            nCategory = 2;
        } else {
            nopDebugStub();                       /* "PQ_error0: <%s>" ch0 @0x410182 */
            continue;
        }
        nPos = 2;                                 /* question scan @0x410094 */
        if (szLine[2] != '?') {
            while (nPos < 0xfd && szLine[nPos] != '?') {
                szQuestion[nPos - 2] = szLine[nPos];
                nPos++;
            }
        }
        szQuestion[nPos - 2] = '?';               /* @0x4100e6 */
        szQuestion[nPos - 1] = '\0';
        if (szLine[nPos + 2] == 'J' || szLine[nPos + 2] == 'j') {       /* @0x4100fa */
            bAnswer = 2;
        } else if (szLine[nPos + 2] == 'N' || szLine[nPos + 2] == 'n') { /* @0x410102 */
            bAnswer = 1;
        } else {
            nopDebugStub();                       /* "PQ_error2: <%s>" ch0 @0x410116 */
            continue;
        }
        nCount++;                                 /* @0x410135 */
        pRec = (QuestRecord *)malloc(0x18);       /* operator_new @0x43dd42 @0x41013a */
        if (pRec != NULL) {
            questRecordCtor(pRec, nCategory, szQuestion, bAnswer); /* @0x40fec0 @0x410161 */
        }
    }
}

/* questPickRandom @0x410290 — count the records of nCategory, then walk
 * the list again and return the (rand() % count + 1)-th match. */
QuestRecord *questPickRandom(int nCategory)
{
    QuestRecord *pCur;
    int nMatch = 0;
    int nPick;

    for (pCur = g_pQuestHead; pCur != NULL; pCur = pCur->pNext) { /* @0x4102a8 */
        if (pCur->nCategory == nCategory) nMatch++;
    }
    if (nMatch == 0) {                            /* @0x4102c2 */
        return NULL;
    }
    nPick = rand() % nMatch + 1;                  /* _rand @0x4102d2 */
    for (pCur = g_pQuestHead; pCur != NULL; pCur = pCur->pNext) { /* @0x4102e6 */
        if (pCur->nCategory == nCategory) nPick--;
        if (nPick < 1) {                          /* @0x4102f5 */
            return pCur;
        }
    }
    return NULL;
}
