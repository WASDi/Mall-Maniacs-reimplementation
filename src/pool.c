#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pool.h"

#define MEM_POOL_MAX 0x100
#define MEM_POOL_LEVELS 0x40
#define MEM_POOL_CHUNKS 0x40
#define MEM_POOL_SLOTS 0x10

typedef struct MemPoolSlot {
    void   *ptr;
    size_t  size;
} MemPoolSlot;

typedef struct MemPoolChunk {
    MemPoolSlot slots[MEM_POOL_SLOTS];
} MemPoolChunk;

typedef struct MemPoolSubPool {
    MemPoolChunk *chunks[MEM_POOL_CHUNKS];
} MemPoolSubPool;

typedef struct MemPool {
    char            name[0x40];
    MemPoolSubPool *subPools[MEM_POOL_LEVELS];
} MemPool;

/* g_apMemPools @0x459f70 — the original 0x100-entry pool-handle table. */
static MemPool *g_apMemPools[MEM_POOL_MAX];

/* memPoolSystemInit @0x4197a0 — zero the pool table, create pool 0 "DEFAULT". */
int memPoolSystemInit(void)
{
    memset(g_apMemPools, 0, sizeof(g_apMemPools));
    (void)memPoolCreate("DEFAULT");   /* s_DEFAULT @0x4500e4 */
    return 1;
}

/* memPoolCreate @0x4197d0 — first free slot; -1 on full or allocation fail. */
int memPoolCreate(char *name)
{
    MemPool *pool;
    int i;
    int length;

    for (i = 0; i < MEM_POOL_MAX; i++) {
        if (g_apMemPools[i] == NULL) break;
    }
    if (i == MEM_POOL_MAX) return -1;

    pool = (MemPool *)malloc(sizeof(*pool));
    if (pool == NULL) return -1;
    g_apMemPools[i] = pool;

    if (name == NULL) {
        pool->name[0] = '\0';
    } else {
        length = 0;
        while (length < 0x3f && name[length] != '\0') {
            pool->name[length] = name[length];
            length++;
        }
        pool->name[length] = '\0';
    }
    memset(pool->subPools, 0, sizeof(pool->subPools));
    return i;
}

/* memPoolAlloc @0x419870 — original 64 subpools × 64 chunks × 16 slots. */
void *memPoolAlloc(int pool, size_t size)
{
    MemPool *poolData;
    MemPoolSubPool *subPool;
    MemPoolChunk *chunk;
    MemPoolSlot *slot;
    void *ptr;
    int subPoolIndex;
    int chunkIndex;
    int slotIndex;

    if ((int)size < 1) return NULL;
    if (g_apMemPools[pool] == NULL) {
        /* Preserve the original invalid-handle sentinel behavior. */
        g_apMemPools[pool] = (MemPool *)(size_t)0x4d2;
    }
    poolData = g_apMemPools[pool];
    subPoolIndex = 0;

    if (poolData->subPools[0] != NULL) {
        do {
            if (subPoolIndex >= MEM_POOL_LEVELS) return NULL;
            subPool = poolData->subPools[subPoolIndex];
            chunkIndex = 0;
            while (chunkIndex < MEM_POOL_CHUNKS &&
                   subPool->chunks[chunkIndex] != NULL) {
                chunk = subPool->chunks[chunkIndex];
                slotIndex = 0;
                while (slotIndex < MEM_POOL_SLOTS &&
                       chunk->slots[slotIndex].size != 0) {
                    slotIndex++;
                }
                if (slotIndex < MEM_POOL_SLOTS) {
                    ptr = malloc(size);
                    if (ptr == NULL) return NULL;
                    slot = &chunk->slots[slotIndex];
                    slot->ptr = ptr;
                    slot->size = size;
                    return ptr;
                }
                chunkIndex++;
            }
            if (chunkIndex < MEM_POOL_CHUNKS) break;
            subPoolIndex++;
        } while (poolData->subPools[subPoolIndex] != NULL);
    }

    if (subPoolIndex == MEM_POOL_LEVELS) return NULL;
    subPool = poolData->subPools[subPoolIndex];
    if (subPool == NULL) {
        subPool = (MemPoolSubPool *)malloc(sizeof(*subPool));
        if (subPool == NULL) return NULL;
        poolData->subPools[subPoolIndex] = subPool;
        memset(subPool, 0, sizeof(*subPool));
        chunkIndex = 0;
        chunk = (MemPoolChunk *)malloc(sizeof(*chunk));
        if (chunk == NULL) return NULL;
        subPool->chunks[chunkIndex] = chunk;
        memset(chunk, 0, sizeof(*chunk));
    } else {
        chunk = (MemPoolChunk *)malloc(sizeof(*chunk));
        if (chunk == NULL) return NULL;
        subPool->chunks[chunkIndex] = chunk;
        memset(&chunk->slots[1], 0,
               (MEM_POOL_SLOTS - 1) * sizeof(chunk->slots[0]));
    }
    ptr = malloc(size);
    if (ptr == NULL) return NULL;
    chunk->slots[0].size = size;
    chunk->slots[0].ptr = ptr;
    return ptr;
}

/* memPoolAllocZero @0x419a20 — allocate, then zero every byte. */
void *memPoolAllocZero(int pool, size_t size)
{
    void *ptr = memPoolAlloc(pool, size);
    if (ptr != NULL) memset(ptr, 0, size);
    return ptr;
}

/* memPoolFree @0x419a60 — search all records; size is the live marker. */
int memPoolFree(int pool, void *ptr)
{
    MemPool *poolData = g_apMemPools[pool];
    int subPoolIndex;

    if (ptr != NULL) {
        for (subPoolIndex = 0; subPoolIndex < MEM_POOL_LEVELS;
             subPoolIndex++) {
            MemPoolSubPool *subPool = poolData->subPools[subPoolIndex];
            int chunkIndex;
            if (subPool == NULL) continue;
            for (chunkIndex = 0; chunkIndex < MEM_POOL_CHUNKS; chunkIndex++) {
                MemPoolChunk *chunk = subPool->chunks[chunkIndex];
                int slotIndex;
                if (chunk == NULL) continue;
                for (slotIndex = 0; slotIndex < MEM_POOL_SLOTS; slotIndex++) {
                    if (chunk->slots[slotIndex].ptr == ptr) {
                        free(ptr);
                        chunk->slots[slotIndex].size = 0;
                        return 1;
                    }
                }
            }
        }
    }
    return 0;
}

/* memPoolDestroy @0x419ae0 — free live blocks and all slab metadata. */
int memPoolDestroy(int pool)
{
    MemPool *poolData = g_apMemPools[pool];
    int subPoolIndex;

    for (subPoolIndex = 0; subPoolIndex < MEM_POOL_LEVELS; subPoolIndex++) {
        MemPoolSubPool *subPool = poolData->subPools[subPoolIndex];
        int chunkIndex;
        if (subPool == NULL) continue;
        for (chunkIndex = 0; chunkIndex < MEM_POOL_CHUNKS; chunkIndex++) {
            MemPoolChunk *chunk = subPool->chunks[chunkIndex];
            int slotIndex;
            if (chunk == NULL) continue;
            for (slotIndex = 0; slotIndex < MEM_POOL_SLOTS; slotIndex++) {
                if (chunk->slots[slotIndex].size != 0) {
                    free(chunk->slots[slotIndex].ptr);
                }
            }
            free(chunk);
        }
        free(subPool);
    }
    free(poolData);
    g_apMemPools[pool] = NULL;
    return 1;
}

/* memPoolSystemShutdown @0x419bb0 — destroy every pool. */
int memPoolSystemShutdown(void)
{
    int pool;

    for (pool = 0; pool < MEM_POOL_MAX; pool++) {
        if (g_apMemPools[pool] != NULL) memPoolDestroy(pool);
    }
    return 1;
}