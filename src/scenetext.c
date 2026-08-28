#include <stdint.h>
#include <string.h>

#include "scenetext.h"

enum { SCENE_TEXT_ANIM_SLOT_COUNT = 16, SCENE_TEXT_GLYPH_SIZE = 16 };

typedef struct __attribute__((packed)) SceneTextAnimRecord {
    int8_t nLoopX;
    int8_t nLoopY;
    int8_t nStepX;
    int8_t nStepY;
    uint8_t nFramePeriod;
    uint8_t nLoopFrame;
    uint8_t nFrame;
    uint8_t nLoopIndex;
    uint16_t nGlyphs;
} SceneTextAnimRecord;

/* g_bSceneTextAnimActive @0x45ecd0 — text-animation manager active flag. */
int g_bSceneTextAnimActive;
/* Slot arrays @0x45ebd0..0x45ec90 — TANI streams and their MAPI glyph bases. */
static void *g_apSceneTextAnimOwner[SCENE_TEXT_ANIM_SLOT_COUNT];
static int g_anSceneTextAnimSize[SCENE_TEXT_ANIM_SLOT_COUNT];
static uint8_t *g_apSceneTextAnimGlyphs[SCENE_TEXT_ANIM_SLOT_COUNT];
static uint8_t *g_apSceneTextAnimData[SCENE_TEXT_ANIM_SLOT_COUNT];

/* sceneTextAnimReset @0x434a50 — clear all sixteen animated-sign slots. */
void sceneTextAnimReset(void)
{
    memset(g_apSceneTextAnimOwner, 0, sizeof(g_apSceneTextAnimOwner));
    memset(g_anSceneTextAnimSize, 0, sizeof(g_anSceneTextAnimSize));
    memset(g_apSceneTextAnimGlyphs, 0, sizeof(g_apSceneTextAnimGlyphs));
    memset(g_apSceneTextAnimData, 0, sizeof(g_apSceneTextAnimData));
    g_bSceneTextAnimActive = 1;
}

/* sceneTextAnimClose @0x434bf0 — clear the manager active flag. Paired with
 * sceneTextAnimReset @0x434a50 by roundStartInit's first (level pre-pass)
 * scene-system cycle. Returns 1 when a flag was cleared, 0 if already off. */
int sceneTextAnimClose(void) /* @0x434bf0 */
{
    if (g_bSceneTextAnimActive == 0) return 0;
    g_bSceneTextAnimActive = 0;
    return 1;
}

/* sceneTextAnimAdd @0x434a90 — register one TANI stream and its MAPI glyphs. */
int sceneTextAnimAdd(void *pOwner, void *pGlyphs, void *pRecordStream, int nStreamLen)
{
    int nSlot;

    if (g_bSceneTextAnimActive == 0) sceneTextAnimReset();
    for (nSlot = 0; nSlot < SCENE_TEXT_ANIM_SLOT_COUNT; nSlot++) {
        if (g_apSceneTextAnimData[nSlot] == NULL) break;
    }
    if (nSlot == SCENE_TEXT_ANIM_SLOT_COUNT) return 0;

    g_apSceneTextAnimOwner[nSlot] = pOwner;
    g_anSceneTextAnimSize[nSlot] = nStreamLen;
    g_apSceneTextAnimGlyphs[nSlot] = pGlyphs;
    g_apSceneTextAnimData[nSlot] = pRecordStream;
    return 1;
}

/* sceneTextAnimUpdate @0x434b00 — advance TANI scrolling/blinking signs. */
void sceneTextAnimUpdate(char nFrameStep)
{
    int nSlot;

    if (g_bSceneTextAnimActive == 0) return;

    for (nSlot = 0; nSlot < SCENE_TEXT_ANIM_SLOT_COUNT; nSlot++) {
        uint8_t *pRecord = g_apSceneTextAnimData[nSlot];
        uint8_t *pEnd;

        if (pRecord == NULL || g_anSceneTextAnimSize[nSlot] < (int)sizeof(SceneTextAnimRecord)) continue;
        pEnd = pRecord + g_anSceneTextAnimSize[nSlot];
        while (pRecord + sizeof(SceneTextAnimRecord) <= pEnd) {
            SceneTextAnimRecord *pAnim = (SceneTextAnimRecord *)pRecord;
            size_t nRecordSize = sizeof(*pAnim) + (size_t)pAnim->nGlyphs * sizeof(uint16_t);
            uint8_t nFrame;
            int8_t nMoveX;
            int8_t nMoveY;
            uint16_t nGlyph;
            uint16_t nGlyphIdx;

            if (nRecordSize > (size_t)(pEnd - pRecord)) break;
            if (pAnim->nFramePeriod == 0) {
                pRecord += nRecordSize;
                continue;
            }

            nFrame = (uint8_t)(pAnim->nFrame + (uint8_t)nFrameStep);
            pAnim->nFrame = nFrame;
            if (nFrame >= pAnim->nFramePeriod) {
                nMoveX = pAnim->nStepX;
                nMoveY = pAnim->nStepY;
                pAnim->nLoopIndex++;
                pAnim->nFrame = (uint8_t)(nFrame % pAnim->nFramePeriod);
                if (pAnim->nLoopIndex == pAnim->nLoopFrame) {
                    pAnim->nLoopIndex = 0;
                    nMoveX = (int8_t)(nMoveX + pAnim->nLoopX);
                    nMoveY = (int8_t)(nMoveY + pAnim->nLoopY);
                }
                for (nGlyphIdx = 0; nGlyphIdx < pAnim->nGlyphs; nGlyphIdx++) {
                    memcpy(&nGlyph, pRecord + sizeof(*pAnim) + nGlyphIdx * sizeof(nGlyph),
                           sizeof(nGlyph));
                    if (g_apSceneTextAnimGlyphs[nSlot] != NULL) {
                        uint8_t *pGlyph = g_apSceneTextAnimGlyphs[nSlot] +
                                          (size_t)nGlyph * SCENE_TEXT_GLYPH_SIZE;
                        pGlyph[8] += nMoveX;
                        pGlyph[10] += nMoveX;
                        pGlyph[12] += nMoveX;
                        pGlyph[14] += nMoveX;
                        pGlyph[9] += nMoveY;
                        pGlyph[11] += nMoveY;
                        pGlyph[13] += nMoveY;
                        pGlyph[15] += nMoveY;
                    }
                }
            }
            pRecord += nRecordSize;
        }
    }
}