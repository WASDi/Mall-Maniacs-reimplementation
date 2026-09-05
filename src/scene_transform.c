#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stddef.h>
#include "scene.h"
#include "scene_transform.h"
#include "gx.h"
#include "pool.h"
#include "zone.h"
#include "sen.h"
#include "util.h"

/* sceneObjSetPos @0x430660 */
int sceneObjSetPos(SceneNode *pObj, int nX, int nY, int nZ, int nMode) /* @0x430660 */
{
    SceneNode *n = pObj;
    SceneChannel *ch = n->pChannels;
    if (nMode == 1) {
        ch->x += nX; ch->y += nY; ch->z += nZ;
    } else if (nMode == 2) {
        ch->x = nX; ch->y = nY; ch->z = nZ;
    } else if (nMode == 5) {
        float sYaw = mathSinDeg(ch->rot[0]);
        float cYaw = mathCosDeg(ch->rot[0]);
        float sPitch = mathSinDeg(ch->rot[1]);
        float cPitch = mathCosDeg(ch->rot[1]);
        float sRoll = mathSinDeg(ch->rot[2]);
        float cRoll = mathCosDeg(ch->rot[2]);
        float matrix[9];
        float x;
        float y;
        float z;

        matrix[0] = sRoll * sPitch * sYaw + cRoll * cPitch;
        matrix[1] = cRoll * sPitch * sYaw - sRoll * cPitch;
        matrix[2] = sPitch * cYaw;
        matrix[3] = sRoll * cYaw;
        matrix[4] = cRoll * cYaw;
        matrix[5] = -sYaw;
        matrix[6] = sRoll * cPitch * sYaw - cRoll * sPitch;
        matrix[7] = cRoll * cPitch * sYaw + sRoll * sPitch;
        matrix[8] = cPitch * cYaw;
        x = (float)nX * matrix[0] + (float)nY * matrix[1]
          + (float)nZ * matrix[2] + (float)ch->x;
        y = (float)nX * matrix[3] + (float)nY * matrix[4]
          + (float)nZ * matrix[5] + (float)ch->y;
        z = (float)nX * matrix[6] + (float)nY * matrix[7]
          + (float)nZ * matrix[8] + (float)ch->z;
        ch->x = (int)x;
        ch->y = (int)y;
        ch->z = (int)z;
    } else return 0;
    ch->bFlagB = 0;
    if (n->pParent != &g_rootNode) sceneNodeUpdateBounds(n->pParent);
    return 1;
}

/* ===================================================================
 * sceneObjSetPosOrient @0x4307d0  (keyframe record application)
 * =================================================================== */
int sceneObjSetPosOrient(SceneNode *pObj, short nYaw, short nPitch, short nRoll, byte nMode) /* @0x4307d0 */
{
    SceneNode *n = pObj;
    SceneChannel *ch = n->pChannels;
    if (nMode & 0x20) { nYaw *= 0xb6; nPitch *= 0xb6; nRoll *= 0xb6; }
    if (nMode & 0x10) return 0;

    switch (nMode & 0xf) {
    case 1:
        ch->rot[0] += nYaw;
        ch->rot[1] += nPitch;
        ch->rot[2] += nRoll;
        break;
    case 2:
        ch->rot[0] = nYaw;
        ch->rot[1] = nPitch;
        ch->rot[2] = nRoll;
        break;
    case 5:
        {
            float sYaw = mathSinDeg(nYaw);
            float cYaw = mathCosDeg(nYaw);
            float sPitch = mathSinDeg(nPitch);
            float cPitch = mathCosDeg(nPitch);
            float sRoll = mathSinDeg(nRoll);
            float cRoll = mathCosDeg(nRoll);
            float forwardX = sPitch * cYaw;
            float forwardY = -sYaw;
            float forwardZ = cPitch * cYaw;
            float viewX;
            float viewZ;
            float invLength;
            float normalX;
            float normalZ;
            float viewY;

            if (ch->bFlagA != 1) chanBuildRotMatrix(ch);
            /* [VERIFIED 2026-08-27] operand pairing from disasm:
             * viewX = fX*m0 + fY*m1 + fZ*m2  (0x4308d7 fwdX*matr[0]@0x1c,
             *             0x4308db fwdZ*matr[2]@0x24, 0x4308e7 fwdY*matr[1]@0x20)
             * viewZ = fX*m6 + fY*m7 + fZ*m8  (0x4308f0..0x430907)
             * viewY = -(fX*m3 + fY*m4 + fZ*m5) (0x430929..0x430944, FCHS)
             * viewX/viewZ/viewY are the true row products of matr (row-major
             * rotation), so a heading delta composes: viewX = sin(dθ)*cosθ +
             * cos(dθ)*sinθ = sin(θ+dθ) -> rot1 accumulates across frames. */
            viewX = forwardX * ch->matr[0] + forwardY * ch->matr[1]
                  + forwardZ * ch->matr[2];
            viewZ = forwardX * ch->matr[6] + forwardY * ch->matr[7]
                  + forwardZ * ch->matr[8];
            invLength = 1.0f / (float)sqrt(viewX * viewX + viewZ * viewZ);
            normalX = invLength * viewX;
            normalZ = invLength * viewZ;
            viewY = -(forwardX * ch->matr[3] + forwardY * ch->matr[4]
                    + forwardZ * ch->matr[5]);

            /* Full look-at Euler decomposition (original @0x430854..0x430a3e,
             * verified instruction-by-instruction incl. FPU stack order).
             * With rollVec2=R1, rollVec=R2, cRoll*cYaw=R3 the original pairs:
             *   rollBasis = R1*matr[0] + R2*matr[2] + R3*matr[1]
             *             (0x430992 FLD ST2=mul matr[0]@0x1c; 0x430997 FLD ST1=
             *              matr[2]@0x24; 0x43099e FLD ST2=matr[1]@0x20 — note
             *              the m2/m1 swap vs matrix index order!)
             *   basisZ    = R1*matr[6] + R2*matr[8] + R3*matr[7] (0x4309a9..ba)
             *   basisY    = R1*matr[4] + R3*matr[3] + R2*matr[5] (0x4309c0..d9)
             * rot0 = atan2(viewY, dot)            (pitch of the view dir)
             * rot1 = atan2(normalX, normalZ)      (yaw/heading of the view dir)
             * rot2 = atan2(normalX*basisZ - normalZ*rollBasis,
             *              basisY*dot + (normalZ*basisZ + normalX*rollBasis)*viewY)
             * With this pairing and yaw=roll=0 inputs, rollBasis==matr[1]==0
             * for yaw-only states, so rot2 stays 0 and (rot0,rot1) converge to
             * a stable fixed point (original shows upright character). */
            {
                float dot = normalX * viewX + normalZ * viewZ;
                float rollVec = sRoll * sPitch + cRoll * cPitch * sYaw;
                float rollVec2 = cRoll * sPitch * sYaw - sRoll * cPitch;
                float rollBasis = rollVec2 * ch->matr[0]
                    + rollVec * ch->matr[2]
                    + cRoll * cYaw * ch->matr[1];
                float basisZ = rollVec2 * ch->matr[6]
                    + rollVec * ch->matr[8]
                    + cRoll * cYaw * ch->matr[7];
                float basisY = rollVec2 * ch->matr[3]
                    + cRoll * cYaw * ch->matr[4]
                    + rollVec * ch->matr[5];

            ch->rot[0] = (short)mathAtan2Deg(viewY, dot);
            ch->rot[1] = (short)mathAtan2Deg(normalX, normalZ);
            ch->rot[2] = (short)mathAtan2Deg(
                -(normalZ * rollBasis - normalX * basisZ),
                basisY * dot + (normalX * rollBasis + normalZ * basisZ) * viewY);
            }
        }
        break;
    default:
        return 0;
    }
    ch->bFlagA = 0;
    ch->bFlagB = 0;
    return 1;
}

/* ===================================================================
 * sceneNodeGetPosWorld @0x430e80
 * =================================================================== */
int sceneNodeGetPosWorld(SceneNode *pNode, float *pOutXYZ, int nMode) /* @0x430e80 */
{
    SceneNode *n = pNode;
    if (n == NULL) {
        /* Safe deviation: cameraFollowUpdate @0x4020d0 passes the follow node
         * (0x4588fc) which is NULL until a local player exists (rebuild runs
         * rounds with g_nPlayerCount == 0 while playerSetupCharacters is
         * stubbed). Callers zero their out buffer. */
        return 0;
    }
    if (nMode == 2) {
        SceneChannel *ch = (SceneChannel *)n->pChannels;
        /* Disasm @0x430e80 mode 2: store the raw int bits of the LOCAL
         * translation (ch->x/y/z at +0x10/+0x14/+0x18) into the float buffer
         * via MOV dword [ECX],EDX (no int->float conversion). Callers read
         * them back as int via ((int*)pOut)[i]: the charselect falling
         * threshold ((int)vec[1] < 0x4e20) and the anim keyframe midpoint
         * interpolation (memcpy float->int) both rely on LOCAL coordinates. */
        ((int *)pOutXYZ)[0] = ch->x;
        ((int *)pOutXYZ)[1] = ch->y;
        ((int *)pOutXYZ)[2] = ch->z;
        return 1;
    }
    if (nMode == 4) {
        /* Disasm @0x430e80 mode 4: accumulator = the node's own channel-0
         * translation (as floats), then walk the ancestor chain starting at
         * pNode->pParent with the channel index taken from the child's
         * channel[0].nIdx, applying the same Ry(r1)*Rx(r0)*Rz(r2) rotation
         * core as sceneNodeGetPos mode 4 (verified operand-by-operand at
         * 0x430f01..0x430fd1) plus each ancestor channel's translation.
         * The root node (pParent == NULL) is never processed. Results are
         * stored via __ftol truncation. */
        SceneChannel *ch0 = pNode->pChannels;
        float px = (float)ch0->x;
        float py = (float)ch0->y;
        float pz = (float)ch0->z;
        SceneNode *par = pNode->pParent;
        int idx = ch0->nIdx;

        while (par->pParent != NULL) {                       /* @0x430ee1 */
            SceneChannel *c = &par->pChannels[idx];
            float s0 = mathSinDeg(c->rot[0]);                /* @0x42d030 @0x430f05 */
            float c0 = mathCosDeg(c->rot[0]);                /* @0x42d050 @0x430f12 */
            float s1 = mathSinDeg(c->rot[1]);                /* @0x430f20 */
            float c1 = mathCosDeg(c->rot[1]);                /* @0x430f2e */
            float s2 = mathSinDeg(c->rot[2]);                /* @0x430f3c */
            float c2 = mathCosDeg(c->rot[2]);                /* @0x430f4a */
            float t = c2 * px - s2 * py;                     /* @0x430f64 */
            float u = c2 * py + s2 * px;                     /* @0x430f74 */
            float v = u * s0 + c0 * pz;                      /* @0x430f86 */
            float w = u * c0 - s0 * pz;                      /* @0x430f96 */
            px = t * c1 + v * s1 + (float)c->x;              /* @0x430faa */
            py = w + (float)c->y;                            /* @0x430fb4 */
            pz = v * c1 - t * s1 + (float)c->z;              /* @0x430fce */
            if (idx == 0) {                                  /* @0x430fd5 */
                par = par->pParent;                          /* @0x430fd7 */
            }
            idx = c->nIdx;                                   /* @0x430fdd */
        }
        /* __ftol @0x43dd10 then MOV dword [ESI],EAX — the truncated int
         * bits go into the buffer (consumers read them back as ints). */
        ((int *)pOutXYZ)[0] = (int)px;                       /* @0x430fec */
        ((int *)pOutXYZ)[1] = (int)py;                       /* @0x430ffb */
        ((int *)pOutXYZ)[2] = (int)pz;                       /* @0x430ff5 */
        return 1;
    }
    return 0;
}

/* ===================================================================
 * sceneNodeGetPos @0x431270 / sceneNodeGetMesh @0x431ae0 /
 * sceneSetCurrentObj @0x430d98 / sceneMeshBBox @0x42ba40 /
 * sceneDetailGridCtor @0x42ad00
 * =================================================================== */

/* Current scene-object context for mode 6 (sceneSetCurrentObj @0x430d98,
 * consumed by playerAnimSfxUpdate). */
SceneNode *g_pSceneNodeHead;   /* @0x45e810 */
int g_nSceneCurrentObj;        /* @0x45e608 */

/* sceneNodeGetMesh @0x431ae0 — return the node's SceneObjTypeDef (+0x20),
 * NULL when the node word0 != 1 or when the type is the shared default type
 * g_sceneObjDefaultType (original constant @0x450f38). */
SceneObjTypeDef *sceneNodeGetMesh(SceneNode *pNode) /* @0x431ae0 */
{
    if (pNode->nId != 1) {
        return NULL;
    }
    if (pNode->pTypeDef == &g_sceneObjDefaultType) {
        return NULL;
    }
    return pNode->pTypeDef;
}

/* sceneSetCurrentObj @0x430d98 — record the scene-object context used by
 * sceneNodeGetPos mode 6. Returns 1. */
int sceneSetCurrentObj(SceneNode *pNodeHead, int nCurrentObj) /* @0x430d98 */
{
    g_pSceneNodeHead = pNodeHead;
    g_nSceneCurrentObj = nCurrentObj;
    return 1;
}

/* sceneNodeGetPos @0x431270 — position query over the channel hierarchy.
 * nMode 2: raw local translation bits (pOut read back as ints by callers).
 * nMode 4: walk the channel-parent chain to g_rootNode accumulating
 *   Ry(r1)*Rx(r0)*Rz(r2) rotations per ancestor (binary-degree sin/cos
 *   @0x42d030/@0x42d050) and the per-channel translation; results stored
 *   as ints via __ftol truncation. For nChannel==0 the walk starts at the
 *   node's parent (channel 0's parent lives one node up).
 * nMode 6: same for this node and for the scene-object context
 *   (g_pSceneNodeHead, g_nSceneCurrentObj), clears bFlagB along the
 *   context's channel chain, recomputes its world transform
 *   (chanCalcWorldTransform @0x42f6e0) and returns
 *   Wmat * (posThis - posHead) as ints. */
int sceneNodeGetPos(SceneNode *pNode, int nChannel, int *pOutXYZ, int nMode) /* @0x431270 */
{
    if (nChannel < 0 || nChannel >= (int)pNode->nChannelCount) {
        return 0;
    }
    if (nMode == 2) {
        SceneChannel *ch = &pNode->pChannels[nChannel];
        ((int *)pOutXYZ)[0] = ch->x;
        ((int *)pOutXYZ)[1] = ch->y;
        ((int *)pOutXYZ)[2] = ch->z;
        return 1;
    }
    if (nMode == 4) {
        SceneChannel *ch = &pNode->pChannels[nChannel];
        float px = (float)ch->x;
        float py = (float)ch->y;
        float pz = (float)ch->z;
        SceneNode *n = (nChannel == 0) ? pNode->pParent : pNode;
        int idx = ch->nIdx;
        while (n != &g_rootNode) {
            SceneChannel *c = &n->pChannels[idx];
            float s0 = mathSinDeg(c->rot[0]);
            float c0 = mathCosDeg(c->rot[0]);
            float s1 = mathSinDeg(c->rot[1]);
            float c1 = mathCosDeg(c->rot[1]);
            float s2 = mathSinDeg(c->rot[2]);
            float c2 = mathCosDeg(c->rot[2]);
            float t = c2 * px - s2 * py;             /* 0x43137a..0x43138f */
            float u = c2 * py + s2 * px;             /* 0x431393..0x43139f */
            float v = u * s0 + c0 * pz;              /* 0x4313a1..0x4313b1 */
            float w = u * c0 - s0 * pz;              /* 0x4313b5..0x4313c1 */
            px = t * c1 + v * s1 + (float)c->x;      /* 0x4313c3..0x4313d8 */
            py = w + (float)c->y;                    /* 0x4313dc..0x4313e1 */
            pz = v * c1 - t * s1 + (float)c->z;      /* 0x4313e7..0x4313fc */
            if (idx == 0) {
                n = n->pParent;                      /* 0x431400..0x431402 */
            }
            idx = c->nIdx;                           /* 0x431405 */
        }
        pOutXYZ[0] = (int)px;                        /* __ftol 0x431414 */
        pOutXYZ[1] = (int)py;                        /* 0x431421 */
        pOutXYZ[2] = (int)pz;                        /* 0x43142c */
        return 1;
    }
    if (nMode == 6) {
        int anPos[3];
        int anHead[3];
        SceneNode *n;
        int idx;
        SceneChannel *hc;
        float dx, dy, dz;
        sceneNodeGetPos(pNode, nChannel, anPos, 4);                  /* 0x431450..0x431459 */
        sceneNodeGetPos(g_pSceneNodeHead, g_nSceneCurrentObj, anHead, 4); /* 0x43145e..0x431472 */
        n = g_pSceneNodeHead;
        idx = g_nSceneCurrentObj;
        while (n != &g_rootNode) {                                   /* 0x431486..0x4314bd */
            SceneChannel *c = &n->pChannels[idx];
            c->bFlagB = 0;                                           /* 0x43149f */
            int next = c->nIdx;
            if (idx == 0) {
                n = n->pParent;                                      /* 0x4314a9..0x4314ac */
            }
            idx = next;
        }
        chanCalcWorldTransform(g_pSceneNodeHead, g_nSceneCurrentObj); /* 0x4314c8 */
        hc = &g_pSceneNodeHead->pChannels[g_nSceneCurrentObj];
        dx = (float)(anPos[0] - anHead[0]);                          /* 0x4314e7..0x431505 */
        dy = (float)(anPos[1] - anHead[1]);
        dz = (float)(anPos[2] - anHead[2]);
        /* out = wmat * diff (column-major 3x3 at hc+0x40), stored as ints */
        pOutXYZ[0] = (int)(dx * hc->wmat[0] + dy * hc->wmat[3] + dz * hc->wmat[6]); /* 0x431522..0x431539 */
        pOutXYZ[1] = (int)(dx * hc->wmat[1] + dy * hc->wmat[4] + dz * hc->wmat[7]); /* 0x431546..0x431557 */
        pOutXYZ[2] = (int)(dx * hc->wmat[2] + dy * hc->wmat[5] + dz * hc->wmat[8]); /* 0x43155f..0x431570 */
        return 1;
    }
    return 0;
}

/* sceneNodeSetPos @0x431590 — position write. nMode 2: raw MOV of the three
 * 32-bit words of pXYZ into channel 0's translation (+0x10/+0x14/+0x18).
 * The channel translation fields are INTS (mode-4 gets FILD them; callers
 * pass ftol'd config ints), so no float conversion happens here. Any other
 * mode only refreshes the parent's bounds. Returns 1 for mode 2, else 0. */
int sceneNodeSetPos(SceneNode *pNode, void *pXYZ, int nMode) /* @0x431590 */
{
    if (nMode == 2) {
        int *pCh = (int *)pNode->pChannels;
        const int *pIn = (const int *)pXYZ;
        pCh[4] = pIn[0];                          /* +0x10 @0x4315a5 */
        pCh[5] = pIn[1];                          /* +0x14 @0x4315ae */
        pCh[6] = pIn[2];                          /* +0x18 @0x4315b8 */
        return 1;
    }
    if (pNode->pParent != &g_rootNode) {              /* 0x4315e2 */
        sceneNodeUpdateBounds(pNode->pParent);        /* 0x4315e9 */
    }
    return 0;
}

/* sceneNodeSetPosShorts @0x431850 — orientation write over channel 0.
 * (nMode & 0xf) == 2: store rot[0..2] shorts (scaled by 0xb6 when bit 0x20
 * of the mode byte is set — 0xb6 = 360/PI/512... the degree encoding used by
 * the engine) and clear the +0xa/+0xb dirty flags so the next world-transform
 * pass rebuilds the matrices. Other modes return 0. */
int sceneNodeSetPosShorts(SceneNode *pNode, short *pAngles, byte nMode) /* @0x431850 */
{
    if ((nMode & 0xf) != 2) {
        return 0;
    }
    SceneChannel *ch = pNode->pChannels;
    if ((nMode & 0x20) == 0x20) {                     /* 0x431862..0x431894 */
        ch->rot[0] = (short)(pAngles[0] * 0xb6);
        ch->rot[1] = (short)(pAngles[1] * 0xb6);
        ch->rot[2] = (short)(pAngles[2] * 0xb6);
    } else {
        ch->rot[0] = pAngles[0];                      /* 0x43189b */
        ch->rot[1] = pAngles[1];
        ch->rot[2] = pAngles[2];
    }
    ch->bFlagA = 0;                                   /* 0x4318a4..0x4318ad */
    ch->bFlagB = 0;
    return 1;
}

/* sceneNodeGetChannelPos @0x4315e0 — channel orientation query.
 * (nMode & 0xf) == 2: copy the channel's raw rot[0..2] shorts.
 * (nMode & 0xf) == 4: world-space orientation via the ancestor-chain
 *   dirty-flag clear + chanCalcWorldTransform @0x42f6e0 and a
 *   sceneMatBuildOrient @0x432c50 decomposition into yaw/pitch/roll shorts
 *   (mathAtan2Deg triple). sceneMatBuildOrient/mathAtan2Deg are not rebuilt
 *   yet, so mode 4 currently falls back to the raw channel shorts — exact
 *   for unrotated hierarchies, which is the only current caller
 *   (levelObjectsCartsCameraInit @0x411b70 on the freshly placed
 *   g_pCamPosNode). TODO: sceneMatBuildOrient + mathAtan2Deg.
 * Mode byte bit 0x20 scales the output shorts by 0xb6. Returns 1 on
 * success, 0 when the channel index is out of range or the mode is
 * unsupported. */
int sceneNodeGetChannelPos(SceneNode *pNode, int nChannel, short *pOutAngles,
                           uint nMode, short *pOutAngles2) /* @0x4315e0 */
{
    if (nChannel >= (int)pNode->nChannelCount) {      /* 0x4315f0: char compare */
        return 0;
    }
    if ((nMode & 0xf) == 2) {                         /* 0x4315f9 */
        SceneChannel *ch = &pNode->pChannels[nChannel];
        pOutAngles[0] = ch->rot[0];
        pOutAngles[1] = ch->rot[1];
        pOutAngles[2] = ch->rot[2];
        pOutAngles2 = pOutAngles;                     /* unify the out ptrs */
    } else if ((nMode & 0xf) == 4) {                  /* 0x43163c */
        /* TODO(0x43163c..0x4316d0): ancestor bFlagB clear walk +
         * chanCalcWorldTransform + sceneMatBuildOrient/@0x432c50 +
         * mathAtan2Deg decomposition. Raw channel shorts stand in until
         * those helpers are rebuilt. */
        SceneChannel *ch = &pNode->pChannels[nChannel];
        pOutAngles2[0] = ch->rot[0];
        pOutAngles2[1] = ch->rot[1];
        pOutAngles2[2] = ch->rot[2];
    } else {
        return 0;
    }
    if ((nMode & 0xf0) == 0x20) {                     /* 0x4316d6..0x4316e2 */
        pOutAngles2[0] = (short)(pOutAngles2[0] * 0xb6);
        pOutAngles2[1] = (short)(pOutAngles2[1] * 0xb6);
        pOutAngles2[2] = (short)(pOutAngles2[2] * 0xb6);
    }
    return 1;
}

/* mathVec2Polar @0x435060 — cartesian in, polar out {length, atan2(y, x)}
 * (FPATAN + FSQRT). Lives here next to the other math* helpers. */
void mathVec2Polar(GxVec2 *pOut, const GxVec2 *pIn) /* @0x435060 */
{
    pOut->y = (float)atan2((double)pIn->y, (double)pIn->x);
    pOut->x = (float)sqrt(pIn->y * pIn->y + pIn->x * pIn->x);
}

/* ===================================================================
 * sceneNodeFacePos @0x431030
 * =================================================================== */
int sceneNodeFacePos(SceneNode *pNode, int nChannel, float flX, float flY, float flZ, int nMode) /* @0x431030 */
{
    SceneNode *n = pNode;
    if (nChannel >= 0 && nChannel < (int)n->nChannelCount && nMode == 2) {
        SceneChannel *ch = &n->pChannels[nChannel];
        float dx = flX - ch->x;
        float dy = flY - ch->y;
        float dz = flZ - ch->z;
        float dHoriz = (float)sqrt(dx * dx + dz * dz);
        float d3 = (float)sqrt(dy * dy + dHoriz * dHoriz);
        /* Original @0x4310a7..0x4310e6: FPATAN on (dx/d, dz/d) resp.
         * (-dy/d3, d/d3) followed by FMUL @0x44b780 (65536/(2*pi)) + ftol —
         * the same binary-degree conversion as mathAtan2Deg. rot[2] forced
         * to 0 (0x431081), bFlagA/bFlagB cleared (0x4310f7/0x431104). */
        ch->rot[1] = (short)mathAtan2Deg(dx, dz);
        ch->rot[0] = (short)mathAtan2Deg(-dy, d3);
        ch->rot[2] = 0;
        ch->bFlagA = 0;
        ch->bFlagB = 0;
        return 1;
    }
    return 0;
}

/* ===================================================================
 * sceneNodeUpdateBounds @0x4303c0
 * Recompute nBoundingRadiusB up the ancestor chain. For each node,
 *   best = (float)nBoundingRadiusA
 *   for each child:
 *     d = sqrt(childChannel.x^2 + y^2 + z^2) + (float)child.nBoundingRadiusB
 *     if (childChannel.nIdx != 0) d += (float)parent.nBoundingRadiusA
 *     best = max(best, d)
 *   if ((float)parent.nBoundingRadiusB == best) stop
 *   parent.nBoundingRadiusB = (int)best  // __ftol truncation
 *   parent = parent->pParent; stop at g_rootNode.
 * Verified vs disasm 0x4303c0..0x430453 (FILD/FSQRT/FIADD/__ftol).
 * =================================================================== */
void sceneNodeUpdateBounds(SceneNode *pNode) /* @0x4303c0 */
{
    SceneNode *cur = pNode;
    while (1) {
        float fBest = (float)cur->nBoundingRadiusA;
        for (SceneNode *child = cur->pChild; child != NULL; child = child->pNextSib) {
            SceneChannel *ch = child->pChannels;
            float fx = (float)ch->x;
            float fy = (float)ch->y;
            float fz = (float)ch->z;
            float d = sqrtf(fx * fx + fy * fy + fz * fz) + (float)child->nBoundingRadiusB;
            if (ch->nIdx != 0) {
                d += (float)cur->nBoundingRadiusA;
            }
            if (fBest < d) {
                fBest = d;
            }
        }
        if ((float)cur->nBoundingRadiusB == fBest) {
            break;
        }
        cur->nBoundingRadiusB = (int)fBest; /* __ftol @0x43dd10 truncation */
        cur = cur->pParent;
        if (cur == &g_rootNode) {
            return;
        }
    }
}

/* ===================================================================
 * sceneObjSetSubPos @0x430a90 — sub-channel orientation/position setter.
 * Original has exactly 6 params (prologue reads entry ESP+0x08..0x18).
 * =================================================================== */
int sceneObjSetSubPos(SceneNode *pObj, int nMeshIdx, short nYaw, short nPitch, short nRoll, byte nMode) /* @0x430a90 */
{
    SceneNode *n = pObj;
    if (nMeshIdx < 0 || nMeshIdx >= (int)n->nChannelCount) return 0;
    if (nMode & 0x20) {
        nYaw *= 0xb6;
        nPitch *= 0xb6;
        nRoll *= 0xb6;
    }
    if (nMode & 0x10) return 0;

    SceneChannel *ch = &n->pChannels[nMeshIdx];
    switch (nMode & 0xf) {
    case 1:
        ch->rot[0] += nYaw;
        ch->rot[1] += nPitch;
        ch->rot[2] += nRoll;
        break;
    case 2:
        ch->rot[0] = nYaw;
        ch->rot[1] = nPitch;
        ch->rot[2] = nRoll;
        break;
    case 5:
        /* [VERIFIED 2026-08-27] identical math to sceneObjSetPosOrient mode 5
         * (@0x430b7c..0x430d1c): fwd=(sP*cY,-sY,cP*cY); viewX=fX*m0+fY*m1+fZ*m2
         * (0x430bb1..0x430bc8); viewZ=fX*m6+fY*m7+fZ*m8; viewY=-(fX*m3+fY*m4+
         * fZ*m5) (0x430c03..0x430c1e); rot0=atan2(viewY,dot) (0x430cd5);
         * rot1=atan2(normalX,normalZ) (0x430ce7); rollBasis=R1*m0+R2*m2+R3*m1
         * (0x430c6c..0x430c7d); basisZ/basisY/V same as sibling; rot2=atan2(
         * -(nZ*rollBasis-nX*basisZ), V) via FCHS + arg overwrite (0x430d0b..d1c). */
        {
            float sYaw = mathSinDeg(nYaw);
            float cYaw = mathCosDeg(nYaw);
            float sPitch = mathSinDeg(nPitch);
            float cPitch = mathCosDeg(nPitch);
            float sRoll = mathSinDeg(nRoll);
            float cRoll = mathCosDeg(nRoll);
            float forwardX = sPitch * cYaw;
            float forwardY = -sYaw;
            float forwardZ = cPitch * cYaw;
            if (ch->bFlagA != 1) chanBuildRotMatrix(ch); /* @0x430ba6..0x430bae */
            float viewX = forwardX * ch->matr[0] + forwardY * ch->matr[1]
                        + forwardZ * ch->matr[2];
            float viewZ = forwardX * ch->matr[6] + forwardY * ch->matr[7]
                        + forwardZ * ch->matr[8];
            float invLength = 1.0f / (float)sqrt(viewX * viewX + viewZ * viewZ);
            float normalX = invLength * viewX;
            float normalZ = invLength * viewZ;
            float viewY = -(forwardX * ch->matr[3] + forwardY * ch->matr[4]
                          + forwardZ * ch->matr[5]);
            float dot = normalX * viewX + normalZ * viewZ;
            float rollVec = sRoll * sPitch + cRoll * cPitch * sYaw;
            float rollVec2 = cRoll * sPitch * sYaw - sRoll * cPitch;
            float rollBasis = rollVec2 * ch->matr[0]
                + rollVec * ch->matr[2]
                + cRoll * cYaw * ch->matr[1];
            float basisZ = rollVec2 * ch->matr[6]
                + rollVec * ch->matr[8]
                + cRoll * cYaw * ch->matr[7];
            float basisY = rollVec2 * ch->matr[3]
                + cRoll * cYaw * ch->matr[4]
                + rollVec * ch->matr[5];

            ch->rot[0] = (short)mathAtan2Deg(viewY, dot);
            ch->rot[1] = (short)mathAtan2Deg(normalX, normalZ);
            ch->rot[2] = (short)mathAtan2Deg(
                -(normalZ * rollBasis - normalX * basisZ),
                basisY * dot + (normalX * rollBasis + normalZ * basisZ) * viewY);
        }
        break;
    default:
        return 0;
    }
    ch->bFlagA = 0;
    ch->bFlagB = 0;
    return 1;
}

/* ===================================================================
 * sceneObjSetSubOrient @0x431110 — interpolated sub-channel orientation.
 * =================================================================== */
int sceneObjSetSubOrient(SceneNode *pObj, int nMeshIdx, short nYaw, short nPitch, short nRoll) /* @0x431110 */
{
    SceneNode *n = pObj;
    if (nMeshIdx < 0 || nMeshIdx >= (int)n->nChannelCount) return 0;
    SceneChannel *ch = &n->pChannels[nMeshIdx];
    float sYaw = mathSinDeg(nYaw);
    float cYaw = mathCosDeg(nYaw);
    float sPitch = mathSinDeg(nPitch);
    float cPitch = mathCosDeg(nPitch);
    float sRoll = mathSinDeg(nRoll);
    float cRoll = mathCosDeg(nRoll);
    float matrix[9];
    int i;

    matrix[0] = sRoll * sPitch * sYaw + cRoll * cPitch;
    matrix[1] = cRoll * sPitch * sYaw - sRoll * cPitch;
    matrix[2] = sPitch * cYaw;
    matrix[3] = sRoll * cYaw;
    matrix[4] = cRoll * cYaw;
    matrix[5] = -sYaw;
    matrix[6] = sRoll * cPitch * sYaw - cRoll * sPitch;
    matrix[7] = cRoll * cPitch * sYaw + sRoll * sPitch;
    matrix[8] = cPitch * cYaw;
    for (i = 0; i < 9; i++) ch->matr[i] = (ch->matr[i] + matrix[i]) * g_flHalf;
    ch->bFlagA = 1;
    return 1;
}

/* ===================================================================
 * sceneNodeFree @0x430460 — mode-gated node free. nFreeChildren is a mode,
 * not a boolean (verified vs disasm: @0x430464 frees children only for
 * mode 1 @0x43046a, @0x4304ae takes the reparent path for mode 3,
 * anything else returns 0 @0x4305aa):
 *   mode 1: free children recursively, then unlink + free (thrownItemFree
 *     @0x40f8d0, charselect preview @0x41f558).
 *   mode 3: splice children into n's slot (first child takes n's position,
 *     siblings reparented to n->parent) and free only n (playerCollectItem
 *     overflow path @0x40f3dd). The original skips first->pParent when the
 *     first child has no next sibling; the rebuild sets it uniformly.
 * The render-list head @0x45e8cc aliases root+0xc, so freeing the head
 * advances it (to the promoted child in mode 3). The @0x430522 clear of
 * g_pSceneNodeHead is the head lifecycle: teardown (sceneFreeAllNodes)
 * clears it when the head node is freed (init does not re-zero it).
 * Mem accounting @0x43053a subtracts the full 0xa8+nSub*0x70. Parent
 * bounds refresh @0x43055f (sceneNodeUpdateBounds @0x4303c0) when parent
 * != root. The nId>=0x100 dtor callback via table @0x45e668 (@0x430572)
 * has no rebuild counterpart; it is unreachable (allocs set nId 1/2/3,
 * root 0).
 * =================================================================== */
void sceneNodeFree(SceneNode *pNode, int nFreeChildren) /* @0x430460 */
{
    SceneNode *n = pNode;
    SceneNode *promoted = NULL;
    int nSub;
    if (!n) return;
    if (nFreeChildren != 1 && nFreeChildren != 3) return;
    if (nFreeChildren == 1) {
        SceneNode *c = n->pChild;
        while (c) { SceneNode *nx = c->pNextSib; sceneNodeFree(c, 1); c = nx; }
    } else if (n->pChild != NULL && n->pParent != NULL) {
        SceneNode *first = n->pChild;
        SceneNode *prev = n->pPrevLink;
        SceneNode *parent = n->pParent;
        SceneNode *last;
        if (prev == parent) parent->pChild = first;
        else prev->pNextSib = first;
        first->pPrevLink = prev;
        first->pParent = parent;
        last = first;
        while (last->pNextSib != NULL) { last = last->pNextSib; last->pParent = parent; }
        last->pNextSib = n->pNextSib;
        if (n->pNextSib != NULL) n->pNextSib->pPrevLink = last;
        promoted = first;
    }
    if (n->pParent != NULL && promoted == NULL) {
        SceneNode *p = n->pParent;
        if (p->pChild == n) p->pChild = n->pNextSib;
        else {
            SceneNode *c = p->pChild;
            while (c && c->pNextSib != n) c = c->pNextSib;
            if (c) c->pNextSib = n->pNextSib;
        }
        if (n->pNextSib != NULL) {
            n->pNextSib->pPrevLink = n->pPrevLink;
        }
    }
    /* The render-list head aliases root+0xc, so the unlink above already
     * advanced it when n was the head (mode-3 promote: parent->pChild = first
     * @0x430490; otherwise p->pChild = n->pNextSib @0x4304b0). */
    if (g_pSceneNodeHead == n) g_pSceneNodeHead = NULL;
    g_nSceneNodeCount--;
    nSub = (int)(signed char)n->nChannelCount - 1;
    if (nSub < 0) nSub = 0;
    g_nSceneNodeMemUsed -= 0xa8 + nSub * 0x70;
    if (n->pParent != &g_rootNode) {
        sceneNodeUpdateBounds(n->pParent);
    }
    free(n);
}

/* ===================================================================
 * mat3x3Mul @0x42f7d0  (out = a * b, column-major storage)
 * Verified against disassembly: out[col*3+row] = Σ_k a[k*3+row]*b[col*3+k].
 * =================================================================== */
void mat3x3Mul(float *a, float *b, float *out)
{
    out[0] = a[0]*b[0] + a[3]*b[1] + a[6]*b[2];
    out[1] = a[1]*b[0] + a[4]*b[1] + a[7]*b[2];
    out[2] = a[2]*b[0] + a[5]*b[1] + a[8]*b[2];
    out[3] = a[0]*b[3] + a[3]*b[4] + a[6]*b[5];
    out[4] = a[1]*b[3] + a[4]*b[4] + a[7]*b[5];
    out[5] = a[2]*b[3] + a[5]*b[4] + a[8]*b[5];
    out[6] = a[0]*b[6] + a[3]*b[7] + a[6]*b[8];
    out[7] = a[1]*b[6] + a[4]*b[7] + a[7]*b[8];
    out[8] = a[2]*b[6] + a[5]*b[7] + a[8]*b[8];
}

/* ===================================================================
 * chanBuildRotMatrix @0x42f030 (build local rotation matrix from euler)
 * Original took short *pRot pointing at channel's rot[3] (int16); scale
 * float at +6. Re-typed to SceneChannel* to avoid GCC
 * -Waddress-of-packed-member (packed->short* conversion) while preserving
 * binary layout (rot[3] at +0, fUnk6 at +6).
 * =================================================================== */
void chanBuildRotMatrix(SceneChannel *ch)
{
    float s0 = mathSinDeg(ch->rot[0]);   /* yaw   */
    float c0 = mathCosDeg(ch->rot[0]);
    float s1 = mathSinDeg(ch->rot[1]);   /* pitch */
    float c1 = mathCosDeg(ch->rot[1]);
    float s2 = mathSinDeg(ch->rot[2]);   /* roll  */
    float c2 = mathCosDeg(ch->rot[2]);
    float f  = ch->fUnk6;
    /* column-major m[col*3+row] (verified vs disasm 0x42f030) */
    ch->matr[0] = (s2*s1*s0 + c2*c1) * f;
    ch->matr[1] = (c2*s1*s0 - s2*c1) * f;
    ch->matr[2] = s1 * c0 * f;
    ch->matr[3] = s2 * c0 * f;
    ch->matr[4] = c2 * c0 * f;
    ch->matr[5] = -s0 * f;
    ch->matr[6] = (s2*c1*s0 - c2*s1) * f;
    ch->matr[7] = (c2*c1*s0 + s2*s1) * f;
    ch->matr[8] = c1 * c0 * f;
    ch->bFlagA = 1;
}

/* ===================================================================
 * chanCalcWorldTransform @0x42f6e0
 * General path handles root node correctly: root->pChannels points to the
 * root channel (g_rootChannel), and sceneBuildRootMatrix sets the root
 * channel's bFlagB=1 so recursion terminates at the root.
 * =================================================================== */
void chanCalcWorldTransform(SceneNode *pNode, int nChannel) /* @0x42f6e0 */
{
    SceneNode *n = pNode;
    if (!n) n = &g_rootNode;
    SceneChannel *ch = &n->pChannels[nChannel];
    if (ch->bFlagB) return;                          /* already computed */
    if (!ch->bFlagA) chanBuildRotMatrix(ch);
    int idx = ch->nIdx;
    SceneChannel *parentWorld;
    if (nChannel == 0) {
        SceneNode *p = n->pParent;
        if (!p) p = &g_rootNode;
        chanCalcWorldTransform(p, idx);
        parentWorld = &p->pChannels[idx];
    } else {
        chanCalcWorldTransform(n, idx);
        parentWorld = &n->pChannels[idx];
    }
    /* mat3x3Mul on packed members would be -Waddress-of-packed-member
     * (matr @0x1c, wmat @0x40 are inside packed SceneChannel). Copy via
     * char+offsetof (no &packed-member) to aligned temporaries. Original
     * MSVC packed(1) build had no such warning — x86 allows unaligned. */
    {
        float aM[9], bM[9], outM[9];
        memcpy(aM, (char *)ch + offsetof(SceneChannel, matr), sizeof(aM));
        memcpy(bM, (char *)parentWorld + offsetof(SceneChannel, wmat), sizeof(bM));
        mat3x3Mul(aM, bM, outM);
        memcpy((char *)ch + offsetof(SceneChannel, wmat), outM, sizeof(outM));
    }
    ch->wx = (float)ch->x * parentWorld->wmat[0] + (float)ch->y * parentWorld->wmat[1] + (float)ch->z * parentWorld->wmat[2] + parentWorld->wx;
    ch->wy = (float)ch->x * parentWorld->wmat[3] + (float)ch->y * parentWorld->wmat[4] + (float)ch->z * parentWorld->wmat[5] + parentWorld->wy;
    ch->wz = (float)ch->x * parentWorld->wmat[6] + (float)ch->y * parentWorld->wmat[7] + (float)ch->z * parentWorld->wmat[8] + parentWorld->wz;
    ch->bFlagB = 1;
}


/* sceneNodeSetHiddenFlag @0x4305c0 — node+0x02 is the bType byte the render
 * gate reads. nMode 1 = set bType 1 (transform+recurse, no draw); 2 = set
 * bType 1 and recurse over the pChild (+0x0c) / pNextSib (+0x08) list;
 * 3 = set bType 2 (sceneNodeRender returns immediately — fully hidden).
 * Returns 1. */
int sceneNodeSetHiddenFlag(SceneNode *pNode, int nMode) /* @0x4305c0 */
{
    if (nMode == 2) {
        SceneNode *pChild = pNode->pChild;
        pNode->bType = 1;
        if (pChild != NULL) {
            do {
                sceneNodeSetHiddenFlag(pChild, 2);
                pChild = pChild->pNextSib;
            } while (pChild != NULL);
        }
        return 1;
    }
    if (nMode == 1) {
        pNode->bType = 1;
        return 1;
    }
    if (nMode == 3) {
        pNode->bType = 2;
    }
    return 1;
}

/* sceneNodeGetHiddenFlag @0x4305b0 — read the node's bType byte (+0x02) as a
 * signed char. Paired with sceneNodeSetHiddenFlag @0x4305c0; the level-event
 * directors poll it to re-arm once the bonus prop is hidden again. */
int sceneNodeGetHiddenFlag(SceneNode *pNode) /* @0x4305b0 */
{
    return (int)(signed char)pNode->bType;
}

/* sceneObjResetFlags @0x430620 — clear the node's renderer flag byte
 * (+0x02 low byte); when nRecursive == 2 also clear the whole child list
 * (children at +0x0c, siblings chained via +0x08). */
int sceneObjResetFlags(SceneNode *pNode, int nRecursive) /* @0x430620 */
{
    SceneNode *pChild;

    pNode->bType = 0;                              /* byte +0x02 @0x430626 */
    if (nRecursive == 2) {                          /* @0x43062a */
        for (pChild = pNode->pChild; pChild != NULL; pChild = pChild->pNextSib) {
            sceneObjResetFlags(pChild, 2);          /* @0x430636 */
        }
    }
    return 1;
}

/* sceneDetailGridSetRoot @0x42b350 — store the viewer/root scene node into
 * grid +4. Called from playerSetupSceneObjects with g_pSceneRoot. */
void sceneDetailGridSetRoot(SceneDetailGrid *pGrid, SceneNode *pRootNode) /* @0x42b350 */
{
    pGrid->nRootNode = (int)(size_t)pRootNode;
}

/* sceneDetailGridAddRow @0x42b000 — register one row (detail level) of
 * scene-mesh handles: capped at grid->nCols, stored into cells[row][col]
 * (column-major: col*nRows + row), duplicates beyond the first column are
 * hidden (sceneNodeSetHiddenFlag 1), missing columns are padded with the
 * last handle, then the first mesh's world position is read into the row
 * buffer (+0x18, stride 0x14: 3 pos ints + 0 / 1) and the row counter is
 * bumped. */
void sceneDetailGridAddRow(SceneDetailGrid *pGrid, int *pHandles, int nCount) /* @0x42b000 */
{
    int col;

    if (nCount <= 0) {
        return;
    }
    if (pGrid->nCols < nCount) {                          /* +0x10 @0x42b014 */
        nCount = pGrid->nCols;
    }
    for (col = 0; col < nCount; col++) {                  /* @0x42b020 */
        pGrid->pCells[col * pGrid->nRows + pGrid->nColsFilled] = pHandles[col]; /* @0x42b027 */
        if (col > 0) {
            sceneNodeSetHiddenFlag((SceneNode *)(uintptr_t)pHandles[col], 1);     /* @0x4305c0 @0x42b038 */
        }
    }
    for (col = nCount; col < pGrid->nCols; col++) {       /* pad with last handle @0x42b04d */
        pGrid->pCells[col * pGrid->nRows + pGrid->nColsFilled] = pHandles[nCount - 1];
    }
    sceneNodeGetPosWorld((SceneNode *)(size_t)pGrid->pCells[pGrid->nColsFilled], /* @0x42b07d */
                         (float *)((char *)pGrid->pRowBuf + pGrid->nColsFilled * 0x14), 4);
    *(int *)((char *)pGrid->pRowBuf + pGrid->nColsFilled * 0x14 + 0xc) = 0;  /* @0x42b096 */
    *(int *)((char *)pGrid->pRowBuf + pGrid->nColsFilled * 0x14 + 0x10) = 1; /* @0x42b09f */
    pGrid->nColsFilled++;                                 /* +0x08 @0x42b0a8 */
}

/* sceneDetailGridFree @0x42b190 — release the grid's three pool buffers
 * (pCells +0x14, pRowBuf +0x18, pColScales +0x1c) via memPoolFree pool 0.
 * The SceneDetailGrid record itself is freed by the caller
 * (roundTeardown @0x40abcf). */
void sceneDetailGridFree(SceneDetailGrid *pGrid) /* @0x42b190 */
{
    if (pGrid->pCells != NULL) {                     /* +0x14 @0x42b194 */
        memPoolFree(0, pGrid->pCells);               /* @0x42b199 */
    }
    if (pGrid->pRowBuf != NULL) {                    /* +0x18 @0x42b19f */
        memPoolFree(0, pGrid->pRowBuf);              /* @0x42b1a4 */
    }
    if (pGrid->pColScales != NULL) {                 /* +0x1c @0x42b1ad */
        memPoolFree(0, pGrid->pColScales);           /* @0x42b1b2 */
    }
}

/* sceneObjSetClassMesh @0x430db0 — re-parent a scene object to a class mesh
 * node. pClassNode == 0 -> g_rootNode. nMode low nibble 3 unlinks the
 * object from its old parent (obj+4) and relinks it into pClassNode's
 * child list (obj+8/+0x10, parent+0xc first-child), updating bounds;
 * nMode low nibble 2 returns 0 without changes. Then stores the mesh idx
 * at *(obj+0x14)+0xc. Returns 0 when nMode&0xf0 == 0x10, the mesh idx is
 * negative or out of the node's channel range (+3 byte). */
int sceneObjSetClassMesh(int pObj, SceneNode *pClassNode, int nMeshIdx, int nMode) /* @0x430db0 */
{
    SceneNode *pObjNode = (SceneNode *)(size_t)pObj;

    if (pClassNode == NULL) {
        pClassNode = &g_rootNode;
    }
    if (((nMode & 0xf0) == 0x10) || nMeshIdx < 0 ||
        nMeshIdx >= (int)pClassNode->nChannelCount) {     /* +3 @0x430dcf */
        return 0;
    }
    if ((nMode & 0xf) == 3) {                             /* @0x430de0 */
        SceneNode *pOldParent = pObjNode->pParent;        /* +0x04 */
        if (pOldParent->pChild == pObjNode) {             /* +0x0c == pObj @0x430de8 */
            pOldParent->pChild = pObjNode->pNextSib;      /* @0x430df2 */
        } else {
            pObjNode->pPrevLink->pNextSib = pObjNode->pNextSib; /* +0x10/+0x08 @0x430df9 */
        }
        if (pObjNode->pNextSib != NULL) {                 /* @0x430e04 */
            pObjNode->pNextSib->pPrevLink = pObjNode->pPrevLink; /* @0x430e0a */
        }
        if (pObjNode->pParent != &g_rootNode) {           /* @0x430e10 */
            sceneNodeUpdateBounds(pObjNode->pParent);     /* @0x4303c0 @0x430e16 */
        }
        pObjNode->pParent = pClassNode;                   /* @0x430e20 */
        pObjNode->pNextSib = pClassNode->pChild;          /* +0x0c @0x430e24 */
        if (pClassNode->pChild != NULL) {
            pClassNode->pChild->pPrevLink = pObjNode;     /* @0x430e2e */
        }
        pClassNode->pChild = pObjNode;                    /* @0x430e34 */
        pObjNode->pPrevLink = pClassNode;                 /* +0x10 @0x430e37 */
        /* Relinking to root also advances the render-list head: it aliases
         * root+0xc (g_pSceneNodeList), so the pClassNode->pChild store above
         * is the head update (e.g. the level-1 burger @0x417b39,
         * sceneObjSetClassMesh(burger, NULL, 0, 3)). */
    } else if ((nMode & 0xf) == 2) {
        return 0;
    }
    *(int *)((char *)pObjNode->pChannels + 0xc) = nMeshIdx; /* *(obj+0x14)+0xc @0x430e3f */
    if (pObjNode->pParent != &g_rootNode) {
        sceneNodeUpdateBounds(pObjNode->pParent);         /* @0x430e46 */
    }
    return 1;
}

/* sceneObjGetPos @0x4317e0 — read the node's rotation channels as three
 * shorts into pOutXYZ (pChannels->rot[0..2]) when (nMode & 0xf) == 2,
 * scaled by 182 when (nMode & 0xf0) == 0x20; returns 1, or 0 for other
 * mode families. Used by itemThrowUpdate to re-orient a landed item's
 * mesh. */
int sceneObjGetPos(SceneNode *pObj, short *pOutXYZ, byte nMode) /* @0x4317e0 */
{
    if ((nMode & 0xf) != 2) {                              /* @0x4317e6 */
        return 0;                                          /* @0x43184c */
    }
    pOutXYZ[0] = pObj->pChannels->rot[0];                  /* @0x4317fe */
    pOutXYZ[1] = pObj->pChannels->rot[1];                  /* +2 @0x43180b */
    pOutXYZ[2] = pObj->pChannels->rot[2];                  /* +4 @0x431817 */
    if ((nMode & 0xf0) == 0x20) {                          /* @0x4317fb/0x43181f */
        pOutXYZ[0] = (short)(pOutXYZ[0] * 0xb6);           /* @0x431828 */
        pOutXYZ[1] = (short)(pOutXYZ[1] * 0xb6);           /* @0x43182d */
        pOutXYZ[2] = (short)(pOutXYZ[2] * 0xb6);           /* @0x431839 */
    }
    return 1;                                              /* @0x431846 */
}
