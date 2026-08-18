#include <string.h>

#include "../src/gx.h"

static int received[4][2];

static void capturePolygon(void *v0, void *v1, void *v2, void *v3,
                           int flags, void *colorUv)
{
    void *vertices[4] = { v0, v1, v2, v3 };
    int i;

    (void)flags;
    (void)colorUv;
    for (i = 0; i < 4; i++) {
        int *vertex = (int *)vertices[i];
        received[i][0] = vertex[0];
        received[i][1] = vertex[1];
    }
}

int main(void)
{
    GxVert v0 = { 10 << 8, 20 << 8 };
    GxVert v1 = { 30 << 8, 20 << 8 };
    GxVert v2 = { 30 << 8, 40 << 8 };
    GxVert v3 = { 10 << 8, 40 << 8 };
    GxColorUv colorUv = { 0 };

    memset(&g_driver, 0, sizeof(g_driver));
    g_driver.api.nSoftwareMode = 1;
    g_driver.api.pDrawPolygon = capturePolygon;

    gxDrawPolygon(&v0, &v1, &v2, &v3, 4, &colorUv);

    if (received[0][0] != 10 || received[0][1] != 20) return 1;
    if (received[1][0] != 30 || received[1][1] != 20) return 2;
    if (received[2][0] != 30 || received[2][1] != 40) return 3;
    if (received[3][0] != 10 || received[3][1] != 40) return 4;

    v0.x = 10 << 8; v0.y = 20 << 8;
    v1.x = 30 << 8; v1.y = 20 << 8;
    v2.x = 30 << 8; v2.y = 40 << 8;
    v3.x = 10 << 8; v3.y = 40 << 8;
    g_driver.api.nSoftwareMode = 0;
    gxDrawPolygon(&v0, &v1, &v2, &v3, 4, &colorUv);

    if (received[0][0] != (10 << 8) || received[0][1] != (20 << 8)) return 5;
    if (received[1][0] != (30 << 8) || received[1][1] != (20 << 8)) return 6;
    if (received[2][0] != (30 << 8) || received[2][1] != (40 << 8)) return 7;
    if (received[3][0] != (10 << 8) || received[3][1] != (40 << 8)) return 8;
    return 0;
}