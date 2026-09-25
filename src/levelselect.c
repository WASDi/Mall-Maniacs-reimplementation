#include "compat_types.h"
#include <math.h>

#include "gx.h"
#include "menu.h"
#include "record.h"
#include "options.h"
#include "charselect.h"
#include "levelselect.h"
#include "custom_helpers.h"
#include "sound.h"
#include "stubs.h"

/* =====================================================================
 * Map-selection subsystem — reimplementation of stateLevelSelect
 * @0x41b900 and stateLevelInit0..4 @0x41b810..0x41b8d0.
 *
 * The screen displays the selected level strip, a locked/unlocked section
 * preview, and the two animated arrows from menu\gfx00.tpg. Right/Left
 * select one of five entries; Enter starts the selected level's original
 * initialization target, and Escape returns to character selection.
 * ===================================================================== */

int g_nLevelSel = 0;             /* @0x45d44c */
int g_nLevelIdx = 0;             /* gameplay level index */
float g_endSceneT = 0.0f;        /* @0x45a768 */

int g_hMenuTexSmal = 0;     /* @0x45a68c */
int g_hMenuTexWood = 0;     /* @0x45a690 */
int g_hMenuTexOrie = 0;     /* @0x45a694 */
int g_hMenuTexAqua = 0;     /* @0x45a698 */
int g_hMenuTexRock = 0;     /* @0x45a69c */
int g_hMenuTexSec100 = 0;   /* @0x45a6a0 */
int g_hMenuTexSec200 = 0;   /* @0x45a6a4 */
int g_hMenuTexSec300 = 0;   /* @0x45a6a8 */
int g_hMenuTexSec400 = 0;   /* @0x45a6ac */
int g_hMenuTexSec500 = 0;   /* @0x45a6b0 */

/* stateLevelInit0 @0x41b810 — select level 0 and enter the original run command. */
int stateLevelInit0(int nType, int nKey, int nKeyType)
{
    (void)nType; (void)nKey; (void)nKeyType;
    g_nLevelIdx = 0;
    playerSetupCharacters();
    unloadGameWorld();
    commandDispatch(0, "run 0");
    return 0;
}

/* stateLevelInit1 @0x41b840 — select level 1 and enter the original run command. */
int stateLevelInit1(int nType, int nKey, int nKeyType)
{
    (void)nType; (void)nKey; (void)nKeyType;
    g_nLevelIdx = 1;
    playerSetupCharacters();
    unloadGameWorld();
    commandDispatch(0, "run 1");
    return 0;
}

/* stateLevelInit2 @0x41b870 — select level 2 and enter the original run command. */
int stateLevelInit2(int nType, int nKey, int nKeyType)
{
    (void)nType; (void)nKey; (void)nKeyType;
    g_nLevelIdx = 2;
    playerSetupCharacters();
    unloadGameWorld();
    commandDispatch(0, "run 2");
    return 0;
}

/* stateLevelInit3 @0x41b8a0 — select level 3 and enter the original run command. */
int stateLevelInit3(int nType, int nKey, int nKeyType)
{
    (void)nType; (void)nKey; (void)nKeyType;
    g_nLevelIdx = 3;
    playerSetupCharacters();
    unloadGameWorld();
    commandDispatch(0, "run 3");
    return 0;
}

/* stateLevelInit4 @0x41b8d0 — select level 4 and enter the original run command. */
int stateLevelInit4(int nType, int nKey, int nKeyType)
{
    (void)nType; (void)nKey; (void)nKeyType;
    g_nLevelIdx = 4;
    playerSetupCharacters();
    unloadGameWorld();
    commandDispatch(0, "run 4");
    return 0;
}

/* stateLevelSelect @0x41b900 — map carousel and level-entry state. */
int stateLevelSelect(int nType, int nKey, int nKeyType)
{
    static PStateFunc levelTargets[5] = {
        stateLevelInit0, stateLevelInit1, stateLevelInit2,
        stateLevelInit3, stateLevelInit4
    };
    static int sectionTextures[5];
    GxVert v0;
    GxVert v1;
    GxVert v2;
    GxVert v3;
    GxColorUv uv;
    unsigned int selected;

    selected = (unsigned int)g_nLevelSel;
    if (selected > 4) selected = 4;

    if (nType == 1 && nKeyType == 2) {
        switch (nKey) {
        case 0:
            sndPlaySfx(0, 1, 2, 0xffff, 0, 0x400);
            selected++;
            if (selected == 5) selected = 4;
            break;
        case 1:
            sndPlaySfx(0, 1, 2, 0xffff, 0, 0x400);
            selected = (selected - 1U) & -(selected != 0);
            break;
        case 6:
            if (g_nLevelCount < (int)selected) {
                sndPlaySfx(0, 1, 5, 0xffff, 0, 0x400);
            } else if (levelTargets[selected] != NULL) {
                sndPlaySfx(0, 1, 3, 0xffff, 0, 0x400);
                g_pStateFunc = levelTargets[selected];
            }
            break;
        case 7:
            sndPlaySfx(0, 1, 4, 0xffff, 0, 0x400);
            g_pStateFunc = stateCharacterSelect;
            break;
        default:
            break;
        }
    }
    g_nLevelSel = (int)selected;

    if (nType != 0) return 0;

    g_endSceneT += g_flFrameDelta * 0.3f;

    if (g_hMenuTexGfx != 0) {
        v0.x = 0x9800; v0.y = 0x3800;
        v1.x = 0xd700; v1.y = 0x3800;
        v2.x = 0xd700; v2.y = 0x5f00;
        v3.x = 0x9800; v3.y = 0x5f00;
        v0.z = v1.z = v2.z = v3.z = 0;
        v0.r = v0.g = v0.b = 0xff;
        v1.r = v1.g = v1.b = 0xff;
        v2.r = v2.g = v2.b = 0xff;
        v3.r = v3.g = v3.b = 0xff;
        uv.nTexture = g_hMenuTexGfx; uv.nParam5 = 0; uv.pad = 0;
        uv.U = 0xb000; uv.V = 0x3300; uv.gwU = 0xef00;
        uv.V2 = 0x3300; uv.gwU2 = 0xef00; uv.hV = 0x5b00;
        uv.U2 = 0xb000; uv.hV2 = 0x5b00;
        gxDrawPolygon(&v0, &v1, &v2, &v3, 0x2004, &uv);
    }
    if (g_hMenuTexLevel != 0) {
        v0.x = 0xe800; v0.y = 0x3200;
        v1.x = 0x1e700; v1.y = 0x3200;
        v2.x = 0x1e700; v2.y = 0x6500;
        v3.x = 0xe800; v3.y = 0x6500;
        v0.z = v1.z = v2.z = v3.z = 0;
        v0.r = v0.g = v0.b = 0xff;
        v1.r = v1.g = v1.b = 0xff;
        v2.r = v2.g = v2.b = 0xff;
        v3.r = v3.g = v3.b = 0xff;
        uv.nTexture = g_hMenuTexLevel; uv.nParam5 = 0; uv.pad = 0;
        uv.U = 0; uv.V = (unsigned short)(selected * 0x3300);
        uv.gwU = 0xff00; uv.V2 = uv.V; uv.gwU2 = 0xff00;
        uv.hV = (unsigned short)((selected + 1) * 0x3300);
        uv.U2 = 0; uv.hV2 = uv.hV;
        gxDrawPolygon(&v0, &v1, &v2, &v3, 0x2004, &uv);
    }

    sectionTextures[0] = g_hMenuTexSec100;
    sectionTextures[1] = g_hMenuTexSec200;
    sectionTextures[2] = g_hMenuTexSec300;
    sectionTextures[3] = g_hMenuTexSec400;
    sectionTextures[4] = g_hMenuTexSec500;
    if (g_nLevelCount >= (int)selected) {
        sectionTextures[0] = g_hMenuTexSmal;
        sectionTextures[1] = g_hMenuTexWood;
        sectionTextures[2] = g_hMenuTexOrie;
        sectionTextures[3] = g_hMenuTexAqua;
        sectionTextures[4] = g_hMenuTexRock;
    }
    if (sectionTextures[selected] != 0) {
        v0.x = 0xc000; v0.y = 0x9700;
        v1.x = 0x1bf00; v1.y = 0x9700;
        v2.x = 0x1bf00; v2.y = 0x19600;
        v3.x = 0xc000; v3.y = 0x19600;
        v0.z = v1.z = v2.z = v3.z = 0;
        v0.r = v0.g = v0.b = 0xff;
        v1.r = v1.g = v1.b = 0xff;
        v2.r = v2.g = v2.b = 0xff;
        v3.r = v3.g = v3.b = 0xff;
        uv.nTexture = sectionTextures[selected]; uv.nParam5 = 0; uv.pad = 0;
        uv.U = 0; uv.V = 0; uv.gwU = 0xff00; uv.V2 = 0;
        uv.gwU2 = 0xff00; uv.hV = 0xff00; uv.U2 = 0; uv.hV2 = 0xff00;
        gxDrawPolygon(&v0, &v1, &v2, &v3, 0x2004, &uv);
    }

    if (g_hMenuTexGfx != 0) {
        if (selected == 4) {
            float arrowOffset = sinf(g_endSceneT) * 5.0f;
            int x = (int)(arrowOffset + 132.0f) << 8;
            v0.x = x; v0.y = 0x10000;
            v1.x = x + 0x2b00; v1.y = 0x10000;
            v2.x = x + 0x2b00; v2.y = 0x12b00;
            v3.x = x; v3.y = 0x12b00;
            v0.z = v1.z = v2.z = v3.z = 0;
            v0.r = v0.g = v0.b = 0xff;
            v1.r = v1.g = v1.b = 0xff;
            v2.r = v2.g = v2.b = 0xff;
            v3.r = v3.g = v3.b = 0xff;
            uv.nTexture = g_hMenuTexGfx; uv.nParam5 = 0; uv.pad = 0;
            uv.U = 0; uv.V = 0x3300; uv.gwU = 0x2b00;
            uv.V2 = 0x3300; uv.gwU2 = 0x2b00; uv.hV = 0x5e00;
            uv.U2 = 0; uv.hV2 = 0x5e00;
            gxDrawPolygon(&v0, &v1, &v2, &v3, 0x2004, &uv);
        } else if (selected == 0) {
            float arrowOffset = sinf(g_endSceneT) * 5.0f;
            int x = (int)(464.0f - arrowOffset);
            gxDrawQuadColor(g_hMenuTexGfx, x, 0x100, x + 0x2b, 0x12b,
                            0x2c, 0x33, 0x57, 0x5e);
        } else {
            float arrowOffset = sinf(g_endSceneT) * 5.0f;
            int x = (int)(arrowOffset + 132.0f);
            gxDrawQuadColor(g_hMenuTexGfx, x, 0x100, x + 0x2b, 0x12b,
                            0, 0x33, 0x2b, 0x5e);
            x = (int)(464.0f - arrowOffset);
            gxDrawQuadColor(g_hMenuTexGfx, x, 0x100, x + 0x2b, 0x12b,
                            0x2c, 0x33, 0x57, 0x5e);
        }
    }
    g_nMenuFadeTarget = 0;
    return 0;
}