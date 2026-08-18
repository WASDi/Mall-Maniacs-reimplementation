#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pool.h"

#define MEM_POOL_MAX 0x100
#define MEM_POOL_NAME_MAX 0x3e

/* g_apMemPools @0x459f70 — one slot per named pool. The original entries
 * refer to slab-allocator metadata; the rebuild keeps a compact ownership
 * list so pool destruction and pool-qualified frees retain the same lifetime
 * contract without reproducing the slab hierarchy. */
typedef struct MemAllocation {
    void                 *ptr;
    struct MemAllocation *next;
} MemAllocation;

typedef struct MemPool {
    char name[MEM_POOL_NAME_MAX + 1];
    MemAllocation *allocations;
} MemPool;

static MemPool *g_apMemPools[MEM_POOL_MAX];

static int poolIndexValid(int pool)
{
    return pool >= 0 && pool < MEM_POOL_MAX;
}

/* memPoolSystemInit @0x4197a0 — zero the pool table, create pool 0 "DEFAULT". */
int memPoolSystemInit(void)
{
    memset(g_apMemPools, 0, sizeof(g_apMemPools));
    (void)memPoolCreate("DEFAULT");   /* s_DEFAULT @0x4500e4, always pool 0 */
    return 1;
}

/* memPoolCreate @0x4197d0 — first free slot; -1 on full or alloc failure. */
int memPoolCreate(char *name)
{
    int i;

    for (i = 0; i < MEM_POOL_MAX; i++) {
        if (g_apMemPools[i] != NULL) continue;
        g_apMemPools[i] = (MemPool *)calloc(1, sizeof(MemPool));
        if (g_apMemPools[i] == NULL) return -1;
        if (name != NULL) {
            strncpy(g_apMemPools[i]->name, name, MEM_POOL_NAME_MAX);
        }
        g_apMemPools[i]->name[MEM_POOL_NAME_MAX] = '\0';
        return i;
    }
    return -1;
}

/* memPoolAlloc @0x419870 — NULL for size<1 or an invalid rebuild handle. */
void *memPoolAlloc(int pool, size_t size)
{
    MemAllocation *allocation;
    void *ptr;

    if (size < 1) return NULL;
    if (!poolIndexValid(pool) || g_apMemPools[pool] == NULL) return NULL;
    ptr = malloc(size);
    if (ptr == NULL) return NULL;
    allocation = (MemAllocation *)malloc(sizeof(*allocation));
    if (allocation == NULL) {
        free(ptr);
        return NULL;
    }
    allocation->ptr = ptr;
    allocation->next = g_apMemPools[pool]->allocations;
    g_apMemPools[pool]->allocations = allocation;
    return ptr;
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
    MemAllocation **link;

    if (ptr == NULL) return 0;
    if (!poolIndexValid(pool) || g_apMemPools[pool] == NULL) return 0;
    link = &g_apMemPools[pool]->allocations;
    while (*link != NULL) {
        MemAllocation *allocation = *link;
        if (allocation->ptr == ptr) {
            *link = allocation->next;
            free(allocation->ptr);
            free(allocation);
            return 1;
        }
        link = &allocation->next;
    }
    return 0;
}

/* memPoolDestroy @0x419ae0 — free slot, returns 0 if already empty. */
int memPoolDestroy(int pool)
{
    MemAllocation *allocation;

    if (!poolIndexValid(pool) || g_apMemPools[pool] == NULL) return 0;
    allocation = g_apMemPools[pool]->allocations;
    while (allocation != NULL) {
        MemAllocation *next = allocation->next;
        free(allocation->ptr);
        free(allocation);
        allocation = next;
    }
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