/* rebuild_driver — 32-bit host for differential tests (tests/difftest).
 *
 * Calls a rebuild function by symbol name with up to 12 raw 32-bit stack
 * arguments (i386 cdecl: a float argument occupies one 4-byte slot, so
 * bit patterns pass through unchanged). Memory the call can see is put
 * at the same addresses the emulator uses for the original binary:
 *   arena  ARENA_BASE  caller-built objects (pointers identical on both sides)
 *   heap   HEAP_BASE   bump allocator behind malloc/calloc/realloc/free
 *                      (-Wl,--wrap), mirrored by the emulator's CRT hooks
 * Globals are copied in and out by rebuild address (globals_map.json).
 * rand() is the MSVC LCG on a seed passed with each call.
 *
 * Binary protocol on stdin/stdout, repeated until EOF:
 *   request:  char name[64]; u32 retKind; u32 nArgs; u32 args[12];
 *             u32 arenaLen; u32 nGlobals; u32 randSeed;
 *             byte arena[arenaLen];
 *             nGlobals x { u32 addr; u32 len; } then the globals' bytes
 *   response: u32 eax; double st0; u32 randSeed; u32 heapUsed; u32 nAllocs;
 *             byte arena[arenaLen]; globals' bytes (same order);
 *             byte heap[heapUsed]; nAllocs x { u32 kind; u32 size; u32 addr; }
 * retKind: 0 = void/int (EAX), 1 = float/double (ST0).
 * Alloc kinds: 0 malloc, 1 calloc, 2 realloc, 3 free (size 0).
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "scene.h"

#define ARENA_BASE 0x10000000u
#define ARENA_SIZE 0x10000u
#define HEAP_BASE  0x20000000u
#define HEAP_SIZE  0x100000u
#define MAX_GLOBALS 4096
#define MAX_ALLOCS  4096

#define MAX_ARGS 12

typedef uint32_t (*IntFn)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                          uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
typedef double (*FltFn)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                        uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

typedef struct Request {
    char     szName[64];
    uint32_t nRetKind;
    uint32_t nArgs;
    uint32_t aArgs[MAX_ARGS];
    uint32_t nArenaLen;
    uint32_t nGlobals;
    uint32_t nRandSeed;
} Request;

typedef struct GlobalRef { uint32_t nAddr, nLen; } GlobalRef;
typedef struct AllocRec { uint32_t nKind, nSize, nAddr; } AllocRec;

/* ---- test-mode allocator and rand, active only around the call ---- */
static int g_bDiffActive;
static uint32_t g_nHeapCur;
static uint32_t g_nRandSeed;
static AllocRec g_aAllocs[MAX_ALLOCS];
static uint32_t g_anAllocSize[MAX_ALLOCS];
static uint32_t g_nAllocs;

void *__real_malloc(size_t n);
void *__real_calloc(size_t n, size_t m);
void *__real_realloc(void *p, size_t n);
void  __real_free(void *p);
int   __real_rand(void);
void  __real_srand(unsigned int nSeed);

static int inHeap(const void *p)
{
    return (uintptr_t)p >= HEAP_BASE && (uintptr_t)p < HEAP_BASE + HEAP_SIZE;
}

static void logAlloc(uint32_t nKind, uint32_t nSize, uint32_t nAddr)
{
    if (g_nAllocs < MAX_ALLOCS) {
        g_aAllocs[g_nAllocs].nKind = nKind;
        g_aAllocs[g_nAllocs].nSize = nSize;
        g_aAllocs[g_nAllocs].nAddr = nAddr;
        g_nAllocs++;
    }
}

/* Same policy as difftest.py's emulator hooks: 16-byte aligned bump. */
static void *bumpAlloc(uint32_t nKind, size_t n)
{
    uint32_t nAddr = g_nHeapCur;
    g_nHeapCur += ((uint32_t)n + 15u) & ~15u;
    if (g_nHeapCur > HEAP_BASE + HEAP_SIZE) abort();
    logAlloc(nKind, (uint32_t)n, nAddr);
    return (void *)(uintptr_t)nAddr;
}

void *__wrap_malloc(size_t n)
{
    return g_bDiffActive ? bumpAlloc(0, n) : __real_malloc(n);
}

void *__wrap_calloc(size_t n, size_t m)
{
    if (!g_bDiffActive) return __real_calloc(n, m);
    void *p = bumpAlloc(1, n * m);
    memset(p, 0, n * m);
    return p;
}

void *__wrap_realloc(void *p, size_t n)
{
    if (!g_bDiffActive && !inHeap(p)) return __real_realloc(p, n);
    void *q = bumpAlloc(2, n);
    if (p) {
        /* Old block size: the logged allocation that returned p. */
        for (uint32_t i = g_nAllocs; i-- > 0;) {
            if (g_aAllocs[i].nAddr == (uint32_t)(uintptr_t)p && g_aAllocs[i].nKind != 3) {
                memcpy(q, p, g_aAllocs[i].nSize < n ? g_aAllocs[i].nSize : n);
                break;
            }
        }
    }
    return q;
}

void __wrap_free(void *p)
{
    /* During a call every free is logged, never executed: blocks may live
     * in the arena, which the real allocator does not own. */
    if (g_bDiffActive || inHeap(p)) {
        if (p) logAlloc(3, 0, (uint32_t)(uintptr_t)p);
        return;
    }
    __real_free(p);
}

int __wrap_rand(void)
{
    if (!g_bDiffActive) return __real_rand();
    g_nRandSeed = g_nRandSeed * 0x343fdu + 0x269ec3u; /* _rand @0x43ea7c */
    return (int)((g_nRandSeed >> 16) & 0x7fff);
}

void __wrap_srand(unsigned int nSeed)
{
    if (!g_bDiffActive) { __real_srand(nSeed); return; }
    g_nRandSeed = nSeed;
}

/* ---- I/O ---- */
static int readAll(void *pBuf, size_t nLen)
{
    size_t nDone = 0;
    while (nDone < nLen) {
        ssize_t n = read(0, (char *)pBuf + nDone, nLen - nDone);
        if (n <= 0) return 0;
        nDone += (size_t)n;
    }
    return 1;
}

static void writeAll(const void *pBuf, size_t nLen)
{
    size_t nDone = 0;
    while (nDone < nLen) {
        ssize_t n = write(1, (const char *)pBuf + nDone, nLen - nDone);
        if (n <= 0) _exit(3);
        nDone += (size_t)n;
    }
}

static void *mapFixed(uint32_t nBase, uint32_t nSize)
{
    void *p = mmap((void *)(uintptr_t)nBase, nSize, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (p != (void *)(uintptr_t)nBase) {
        fprintf(stderr, "rebuild_driver: cannot map 0x%x\n", nBase);
        exit(2);
    }
    return p;
}

static GlobalRef g_aGlobals[MAX_GLOBALS];

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--layout") == 0) {
        /* Layout facts the Python side checks against Ghidra. */
        printf("SceneChannel.size=0x%x\n", (unsigned)sizeof(SceneChannel));
        printf("SceneChannel.x=0x%x\n", (unsigned)offsetof(SceneChannel, x));
        printf("SceneNode.nChannelCount=0x%x\n", (unsigned)offsetof(SceneNode, nChannelCount));
        printf("SceneNode.pChannels=0x%x\n", (unsigned)offsetof(SceneNode, pChannels));
        return 0;
    }

    void *pArena = mapFixed(ARENA_BASE, ARENA_SIZE);
    void *pHeap = mapFixed(HEAP_BASE, HEAP_SIZE);

    Request req;
    while (readAll(&req, sizeof(req))) {
        if (req.nArenaLen > ARENA_SIZE || req.nArgs > MAX_ARGS || req.nGlobals > MAX_GLOBALS) return 4;
        if (!readAll(pArena, req.nArenaLen)) return 5;
        if (!readAll(g_aGlobals, req.nGlobals * sizeof(GlobalRef))) return 5;
        for (uint32_t i = 0; i < req.nGlobals; i++)
            if (!readAll((void *)(uintptr_t)g_aGlobals[i].nAddr, g_aGlobals[i].nLen)) return 5;
        req.szName[63] = 0;
        void *pFn = dlsym(RTLD_DEFAULT, req.szName);
        if (!pFn) {
            fprintf(stderr, "rebuild_driver: unknown symbol %s\n", req.szName);
            return 6;
        }

        memset(pHeap, 0, HEAP_SIZE);
        g_nHeapCur = HEAP_BASE;
        g_nAllocs = 0;
        g_nRandSeed = req.nRandSeed;
        g_bDiffActive = 1;
        uint32_t *a = req.aArgs;
        uint32_t nEax = 0;
        double dblSt0 = 0.0;
        if (req.nRetKind == 1)
            dblSt0 = ((FltFn)pFn)(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11]);
        else
            nEax = ((IntFn)pFn)(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11]);
        g_bDiffActive = 0;

        uint32_t nHeapUsed = g_nHeapCur - HEAP_BASE;
        writeAll(&nEax, 4);
        writeAll(&dblSt0, 8);
        writeAll(&g_nRandSeed, 4);
        writeAll(&nHeapUsed, 4);
        writeAll(&g_nAllocs, 4);
        writeAll(pArena, req.nArenaLen);
        for (uint32_t i = 0; i < req.nGlobals; i++)
            writeAll((void *)(uintptr_t)g_aGlobals[i].nAddr, g_aGlobals[i].nLen);
        writeAll(pHeap, nHeapUsed);
        writeAll(g_aAllocs, g_nAllocs * sizeof(AllocRec));
    }
    return 0;
}
