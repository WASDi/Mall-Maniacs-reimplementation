#ifndef QUEST_H
#define QUEST_H

/* quest.h — frog question records (questLoad @0x40ffa0). quest.txt is
 * XOR-0x55 obfuscated; every "1|2|3 <question>? J|N" line becomes a
 * 0x18-byte QuestRecord prepended to g_pQuestHead. Records are consumed
 * by questPickRandom (the frog-message logic) and unlinked one by one
 * via questRecordDtor (roundTeardown @0x40aa10). */

typedef struct QuestRecord {
    int nPad0;                  /* +0x00 unused (zeroed by the allocator) */
    struct QuestRecord *pNext;  /* +0x04 */
    struct QuestRecord *pPrev;  /* +0x08 */
    int  nCategory;             /* +0x0c 0/1/2 from the leading '1'/'2'/'3' */
    char *pszQuestion;          /* +0x10 pool copy incl. the trailing '?' */
    int  bAnswer;               /* +0x14 1 = N, 2 = J */
} QuestRecord;                  /* 0x18 */

extern QuestRecord *g_pQuestHead;  /* @0x458978 */
extern QuestRecord *g_pQuestTail;  /* @0x45897c */

/* questRecordCtor @0x40fec0 — fill + prepend (thiscall in the original). */
QuestRecord *questRecordCtor(QuestRecord *pRec, int nCategory,
                             const char *pszQuestion, int bAnswer);

/* questRecordDtor @0x40ff50 — unlink one record + free its question
 * (original __fastcall: record in ECX). */
void questRecordDtor(QuestRecord *pRec);

/* questLoad @0x40ffa0 — parse quest.txt into the record list. */
void questLoad(const char *pszPath);

/* questPickRandom @0x410290 — random record of a category (rand() % n). */
QuestRecord *questPickRandom(int nCategory);

#endif /* QUEST_H */
