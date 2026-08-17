#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pool.h"

#define MEM_POOL_MAX 0x100
#define MEM_POOL_NAME_MAX 0x3e

/* g_apMemPools @0x459f70 — one slot per named pool. In the original each
 * entry is a slab-allocator handle; here it is a small name-only record. */
typedef struct MemPool {
    char name[MEM_POOL_NAME_MAX + 1];
} MemPool;

static MemPool *g_apMemPools[MEM_POOL_MAX];

/* memPoolSystemInit @0x4197a0 — zero the pool table, create pool 0 "DEFAULT". */
int memPoolSystemInit(void)
{
    memset(g_apMemPools, 0, sizeof(g_apMemPools));
    return memPoolCreate("DEFAULT");   /* s_DEFAULT @0x4500e4, always pool 0 */
}

/* memPoolCreate @0x4197d0 — first free slot; -1 on full or alloc failure. */
int memPoolCreate(char *name)
{
    int i;

    if (name == NULL) return -1;
    for (i = 0; i < MEM_POOL_MAX; i++) {
        if (g_apMemPools[i] != NULL) continue;
        g_apMemPools[i] = (MemPool *)malloc(sizeof(MemPool));
        if (g_apMemPools[i] == NULL) return -1;
        strncpy(g_apMemPools[i]->name, name, MEM_POOL_NAME_MAX);
        g_apMemPools[i]->name[MEM_POOL_NAME_MAX] = '\0';
        return i;
    }
    return -1;
}

/* memPoolAlloc @0x419870 — NULL for size<1 or nonexistent pool. */
void *memPoolAlloc(int pool, size_t size)
{
    if (size < 1) return NULL;
    if (g_apMemPools[pool] == NULL) return NULL;   /* pool must exist */
    return malloc(size);
}

/* memPoolAllocZero @0x419a20 — allocate + zero. */
void *memPoolAllocZero(int pool, size_t size)
{
    void *p = memPoolAlloc(pool, size);
    if (p != NULL) memset(p, 0, size);
    return p;
}

/* memPoolFree @0x419a60 — free; returns 0 if ptr NULL. */
int memPoolFree(int pool, void *ptr)
{
    (void)pool;
    if (ptr == NULL) return 0;
    free(ptr);
    return 1;
}

/* memPoolDestroy @0x419ae0 — free slot, returns 0 if already empty. */
int memPoolDestroy(int pool)
{
    if (g_apMemPools[pool] == NULL) return 0;
    free(g_apMemPools[pool]);
    g_apMemPools[pool] = NULL;
    return 1;
}

/* memPoolSystemShutdown @0x419bb0 — destroy every pool. */
int memPoolSystemShutdown(void)
{
    int i;
    for (i = 0; i < MEM_POOL_MAX; i++) {
        if (g_apMemPools[i] != NULL) memPoolDestroy(i);
    }
    return 1;
}