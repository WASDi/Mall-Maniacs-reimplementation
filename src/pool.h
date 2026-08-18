#ifndef POOL_H
#define POOL_H

#include <windows.h>

/* memPool cluster — reimplementation of maniac memPool* @0x4197a0-0x419bb0.
 *
 * The original is a hierarchical slab allocator over a per-pool structure.
 * This rebuild keeps the pool-handle interface with plain allocations plus
 * an ownership list: pool destruction reclaims live allocations and a free
 * succeeds only for a pointer owned by the selected pool. Slab layout and the
 * original invalid-handle failure behavior remain deferred. */

int  memPoolSystemInit(void);       /* @0x4197a0, returns 1 after init */
int  memPoolCreate(char *name);     /* @0x4197d0  returns pool index, -1 on fail */
void *memPoolAlloc(int pool, size_t size);      /* @0x419870 */
void *memPoolAllocZero(int pool, size_t size);  /* @0x419a20 */
int  memPoolFree(int pool, void *ptr);          /* @0x419a60 */
int  memPoolDestroy(int pool);      /* @0x419ae0 */
int  memPoolSystemShutdown(void);   /* @0x419bb0 */

#endif /* POOL_H */