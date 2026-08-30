#include "player.h"

/* g_playerRecords @0x456210 — the 0x374-byte gameplay player records.
 * The controller view (original base 0x456524) is the embedded
 * PlayerRecord.ai member. */
PlayerRecord g_playerRecords[8];

/* g_nResultsScreen @0x458130 — results-screen gate in gameWorldUpdate. */
int g_nResultsScreen;
