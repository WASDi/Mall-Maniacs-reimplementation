#include <stdio.h>

#include "pool.h"
#include "util.h"

int main(void)
{
    int poolA;
    int poolB;
    FILE *fp;
    char data[4] = { 0 };
    void *ptr;

    if (memPoolSystemInit() != 1) return 1;
    poolA = memPoolCreate("A");
    poolB = memPoolCreate(NULL);
    if (poolA < 0 || poolB < 0) return 2;
    ptr = memPoolAlloc(poolA, 8);
    if (ptr == NULL) return 3;
    if (memPoolFree(poolB, ptr) != 0) return 4;
    if (memPoolFree(poolA, ptr) != 1) return 5;
    if (memPoolDestroy(poolA) != 1) return 6;
    if (memPoolDestroy(poolB) != 1) return 7;

    fp = tmpfile();
    if (fp == NULL) return 8;
    fputs("abc", fp);
    fflush(fp);
    fileSeekTell(fp, 0, 0);
    fileReadN(fp, data, 3);
    if (data[0] != 'a' || data[2] != 'c') return 9;
    fileSeekTell(fp, 0, 99);
    if (fgetc(fp) != EOF) return 10;
    fclose(fp);
    memPoolSystemShutdown();
    return 0;
}