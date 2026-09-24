#include <math.h>

#include "../src/menu.h"
#include "../src/levelselect.h"
#include "../src/options.h"
#include "../src/gx.h"

extern int g_nLevelSel;
extern float g_endSceneT;
extern int g_hMenuTexLevel;

static int drawCount;
static int arrowX[2];

static void capturePolygon(void *v0, void *v1, void *v2, void *v3,
                           int flags, void *colorUv)
{
    (void)v1; (void)v2; (void)v3; (void)flags; (void)colorUv;
    if (drawCount == 1 || drawCount == 2) {
        arrowX[drawCount - 1] = ((GxVert *)v0)->x >> 8;
    }
    drawCount++;
}

static int captureArrowPositions(float phase, int leftX, int rightX)
{
    drawCount = 0;
    g_endSceneT = phase;
    stateLevelSelect(0, 0, 0);
    if (drawCount != 3) {
        fprintf(stderr, "expected 3 draws, got %d\n", drawCount);
        return 1;
    }
    if (arrowX[0] != leftX) {
        fprintf(stderr, "expected left x=%d, got %d\n", leftX, arrowX[0]);
        return 2;
    }
    if (arrowX[1] != rightX) {
        fprintf(stderr, "expected right x=%d, got %d\n", rightX, arrowX[1]);
        return 3;
    }
    return 0;
}

int main(void)
{
    int result;

    g_nLevelSel = 1;
    g_flFrameDelta = 0.0f;
    g_hMenuTexGfx = 1;
    g_hMenuTexLevel = 0;
    g_hMenuTexSmal = 0;
    g_hMenuTexWood = 0;
    g_hMenuTexOrie = 0;
    g_hMenuTexAqua = 0;
    g_hMenuTexRock = 0;
    g_hMenuTexSec100 = 0;
    g_hMenuTexSec200 = 0;
    g_hMenuTexSec300 = 0;
    g_hMenuTexSec400 = 0;
    g_hMenuTexSec500 = 0;
    g_driver.api.nSoftwareMode = 0;
    g_driver.api.pDrawPolygon = capturePolygon;

    result = captureArrowPositions((float)(M_PI * 0.5), 137, 459);
    if (result != 0) return result;
    result = captureArrowPositions((float)(M_PI * 1.5), 127, 469);
    if (result != 0) return result + 3;
    return 0;
}