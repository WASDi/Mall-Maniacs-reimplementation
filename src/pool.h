#ifndef POOL_H
#define POOL_H

#include <windows.h>

/* memPool cluster — reimplementation of maniac memPool* @0x4197a0-0x419bb0.
 *
 * The original is a hierarchical slab allocator over a per-pool structure
 * (0x140 bytes: 0x40-byte name @+0x00, 0x40 slots of 0x40-byte blocks @+0x40,
 * growable arena). Per Rebuild.md the implementation is simplified to plain
 * malloc/free while preserving the pool-handle interface: callers allocate
 * from a named pool (e.g. pool 0 "DEFAULT", g_fontPool "FONT") and free to
 * the same pool. Pool membership is not enforced. */

int  memPoolSystemInit(void);       /* @0x4197a0 */
int  memPoolCreate(char *name);     /* @0x4197d0  returns pool index, -1 on fail */
void *memPoolAlloc(int pool, size_t size);      /* @0x419870 */
void *memPoolAllocZero(int pool, size_t size);  /* @0x419a20 */
int  memPoolFree(int pool, void *ptr);          /* @0x419a60 */
int  memPoolDestroy(int pool);      /* @0x419ae0 */
int  memPoolSystemShutdown(void);   /* @0x419bb0 */

#endif /* POOL_H */