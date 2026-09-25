/* test_anim_lerp.c — offline unit test for linear keyframe interpolation
 * (animClampT / animLerpShort / animLerpInt / sceneObjectAnimStepLerp).
 *
 * Uses a hand-built 2-frame animation (no assets, no display):
 *   frame 0: type-5 ch1 {10,20,30}, ch2 {100,0,0}, type-6 {1000,2000,3000}
 *   frame 1: type-5 ch1 {20,40,50} (ch2 omitted: hold), type-6 {1010,2020,3030}
 * Checks scalar clamping/truncation, midpoint blending with hold semantics,
 * the runtime Y/Z sign conversion on positions, t=0 snap equivalence, loop
 * wrap-around blending, and the non-loop end-of-stream contract. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/anim.h"
#include "../src/scene.h"

#define NCH 16

static SceneNode *makeNode(void)
{
    size_t size = 0x38u + (size_t)NCH * sizeof(SceneChannel);
    SceneNode *n = (SceneNode *)malloc(size);
    if (!n) return NULL;
    memset(n, 0, size);
    n->nChannelCount = NCH;
    n->pChannels = (SceneChannel *)((char *)n + 0x38u);
    n->pParent = &g_rootNode;
    n->pChild = NULL;
    return n;
}

static void putS16(unsigned char *p, short v)
{
    memcpy(p, &v, sizeof(v));
}

static void putS32(unsigned char *p, int v)
{
    memcpy(p, &v, sizeof(v));
}

/* Frame 0 record block: ch1, ch2, pos. Frame 1: ch1, pos. */
static unsigned char g_rec0[8 + 8 + 16];
static unsigned char g_rec1[8 + 16];
static AnmTrack g_tracks[2];
static AnmFile g_anm;

static void buildAnim(void)
{
    unsigned char *p = g_rec0;
    p[0] = 5; p[1] = 1; putS16(p + 2, 10); putS16(p + 4, 20); putS16(p + 6, 30);
    p += 8;
    p[0] = 5; p[1] = 2; putS16(p + 2, 100); putS16(p + 4, 0); putS16(p + 6, 0);
    p += 8;
    p[0] = 6; p[1] = 0; p[2] = 0; p[3] = 0;
    putS32(p + 4, 1000); putS32(p + 8, 2000); putS32(p + 12, 3000);

    p = g_rec1;
    p[0] = 5; p[1] = 1; putS16(p + 2, 20); putS16(p + 4, 40); putS16(p + 6, 50);
    p += 8;
    p[0] = 6; p[1] = 0; p[2] = 0; p[3] = 0;
    putS32(p + 4, 1010); putS32(p + 8, 2020); putS32(p + 12, 3030);

    g_tracks[0].nRecs = 3;
    g_tracks[0].pRecs = g_rec0;
    g_tracks[1].nRecs = 2;
    g_tracks[1].pRecs = g_rec1;

    memset(&g_anm, 0, sizeof(g_anm));
    g_anm.nFrame = 0;
    g_anm.nFrameCount = 2;
    g_anm.pTrackBase = g_tracks;
    g_anm.pCurTrack = g_tracks;
    g_anm.pObj = NULL;
    g_anm.pMasterNode = NULL;
}

static int check(int cond, int code)
{
    if (!cond) {
        printf("FAIL %d\n", code);
        return code;
    }
    return 0;
}

int main(void)
{
    SceneNode *node;
    SceneObjAnimList list;
    AnmFile anm;
    int rc, fail;

    /* --- scalar helpers --- */
    if ((fail = check(animClampT(-1.0f) == 0.0f, 1))) return fail;
    if ((fail = check(animClampT(2.0f) == 1.0f, 2))) return fail;
    if ((fail = check(animClampT(0.25f) == 0.25f, 3))) return fail;
    if ((fail = check(animLerpShort(10, 20, 0.5f) == 15, 4))) return fail;
    if ((fail = check(animLerpShort(10, 21, 0.5f) == 15, 5))) return fail; /* 10+5.5 -> trunc 15 */
    if ((fail = check(animLerpShort(0, 1, 0.5f) == 0, 6))) return fail;    /* trunc toward zero */
    if ((fail = check(animLerpShort(0, -1, 0.5f) == 0, 7))) return fail;   /* trunc toward zero */
    if ((fail = check(animLerpShort(-4, -1, 0.5f) == -2, 8))) return fail; /* -4+1.5=-2.5 -> -2 */
    if ((fail = check(animLerpShort(10, 20, 2.0f) == 20, 9))) return fail; /* clamped */
    if ((fail = check(animLerpShort(10, 20, -1.0f) == 10, 10))) return fail;
    if ((fail = check(animLerpInt(1000, 1010, 0.5f) == 1005, 11))) return fail;
    if ((fail = check(animLerpInt(1000, 1010, 0.0f) == 1000, 12))) return fail;
    if ((fail = check(animLerpInt(1000, 1010, 1.0f) == 1010, 13))) return fail;

    buildAnim();

    /* --- t=0.5 midpoint on frame 0 (loop): ch1 lerps, ch2 holds,
     *     position lerps raw then Y/Z negates on apply --- */
    node = makeNode();
    if (!node) return 20;
    memset(&list, 0, sizeof(list));
    list.apObjs[0] = node;
    anm = g_anm;
    list.pState = (SceneObjAnimState *)&anm;
    rc = sceneObjectAnimStepLerp(&list, 1, 0.5f);
    if ((fail = check(rc == 0, 21))) return fail;
    if ((fail = check(anm.nFrame == 1, 22))) return fail;
    if ((fail = check(node->pChannels[1].rot[0] == 15 &&
                      node->pChannels[1].rot[1] == 30 &&
                      node->pChannels[1].rot[2] == 40, 23))) return fail;
    if ((fail = check(node->pChannels[2].rot[0] == 100 &&
                      node->pChannels[2].rot[1] == 0 &&
                      node->pChannels[2].rot[2] == 0, 24))) return fail;
    if ((fail = check(node->pChannels[0].x == 1005 &&
                      node->pChannels[0].y == -2010 &&
                      node->pChannels[0].z == -3015, 25))) return fail;

    /* --- second step wraps (loop): blends frame 1 -> frame 0 --- */
    rc = sceneObjectAnimStepLerp(&list, 1, 0.5f);
    if ((fail = check(rc == 1, 26))) return fail;
    if ((fail = check(anm.nFrame == 0, 27))) return fail;
    if ((fail = check(node->pChannels[1].rot[0] == 15 &&
                      node->pChannels[1].rot[1] == 30 &&
                      node->pChannels[1].rot[2] == 40, 28))) return fail;
    if ((fail = check(node->pChannels[0].x == 1005 &&
                      node->pChannels[0].y == -2010 &&
                      node->pChannels[0].z == -3015, 29))) return fail;
    free(node);

    /* --- t=0 reproduces the exact snap pose of the current frame --- */
    node = makeNode();
    if (!node) return 30;
    memset(&list, 0, sizeof(list));
    list.apObjs[0] = node;
    anm = g_anm;
    list.pState = (SceneObjAnimState *)&anm;
    rc = sceneObjectAnimStepLerp(&list, 1, 0.0f);
    if ((fail = check(rc == 0, 31))) return fail;
    if ((fail = check(node->pChannels[1].rot[0] == 10 &&
                      node->pChannels[1].rot[1] == 20 &&
                      node->pChannels[1].rot[2] == 30, 32))) return fail;
    if ((fail = check(node->pChannels[0].x == 1000 &&
                      node->pChannels[0].y == -2000 &&
                      node->pChannels[0].z == -3000, 33))) return fail;
    free(node);

    /* --- non-loop: last frame holds (next == current), end returns 1 --- */
    node = makeNode();
    if (!node) return 40;
    memset(&list, 0, sizeof(list));
    list.apObjs[0] = node;
    anm = g_anm;
    list.pState = (SceneObjAnimState *)&anm;
    rc = sceneObjectAnimStepLerp(&list, 0, 0.5f);
    if ((fail = check(rc == 0, 41))) return fail;
    anm.nFrame = 1; /* jump state to last frame like the stepper leaves it */
    anm.pCurTrack = &g_tracks[1];
    memset(node->pChannels, 0, (size_t)NCH * sizeof(SceneChannel));
    rc = sceneObjectAnimStepLerp(&list, 0, 0.5f);
    if ((fail = check(rc == 1, 42))) return fail;
    if ((fail = check(node->pChannels[1].rot[0] == 20 &&
                      node->pChannels[1].rot[1] == 40 &&
                      node->pChannels[1].rot[2] == 50, 43))) return fail;
    if ((fail = check(node->pChannels[2].rot[0] == 100, 44))) return fail; /* hold from frame 0 */
    if ((fail = check(node->pChannels[0].x == 1010 &&
                      node->pChannels[0].y == -2020 &&
                      node->pChannels[0].z == -3030, 45))) return fail;
    free(node);

    printf("anim_lerp OK\n");
    return 0;
}
