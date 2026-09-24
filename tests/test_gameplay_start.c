/* test_gameplay_start.c — SDL2 port: runCmd @0x4084c0 dispatch contract.
 *
 * Full round start (roundStartInit: SEN/TPG/scene + GL) needs assets and
 * a real display context, so it is covered by the launch verification
 * (Xvfb + Mesa), not here. This test covers the pure dispatch gates:
 * bad input and already-active rounds never start a new round. */

#include "../src/gameplay.h"
#include "../src/levelselect.h"

int main(void)
{
    g_bGameActive = 0;
    g_nLevelIdx = 3;

    /* Bad (non-numeric) args: no round starts, level untouched. */
    runCmd(0, "bad");
    if (g_bGameActive != 0 || g_nLevelIdx != 3) return 1;

    /* NULL args: same. */
    runCmd(0, NULL);
    if (g_bGameActive != 0 || g_nLevelIdx != 3) return 2;

    /* Active round: new run commands are ignored. */
    g_bGameActive = 1;
    runCmd(0, "4");
    if (g_nLevelIdx != 3) return 3;
    g_bGameActive = 0;

    return 0;
}
