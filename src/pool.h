#ifndef POOL_H
#define POOL_H

#include <windows.h>

/* memPool cluster — reimplementation of maniac memPool* @0x4197a0-0x419bb0.
 *
 * The rebuild retains the original 64-subpool / 64-chunk / 16-slot hierarchy.
 * Each slot records a block pointer and size; size zero marks a reusable slot.
 * Invalid nonzero handles intentionally retain the original failure behavior. */

int  memPoolSystemInit(void);       /* @0x4197a0, returns 1 after init */
int  memPoolCreate(char *name);     /* @0x4197d0  returns pool index, -1 on fail */
void *memPoolAlloc(int pool, size_t size);      /* @0x419870 */
void *memPoolAllocZero(int pool, size_t size);  /* @0x419a20 */
int  memPoolFree(int pool, void *ptr);          /* @0x419a60 */
int  memPoolDestroy(int pool);      /* @0x419ae0 */
int  memPoolSystemShutdown(void);   /* @0x419bb0 */

#endif /* POOL_H */