#ifndef RECORD_H
#define RECORD_H

#include "compat_types.h"

/* Rekord / high-score table screen ("Rekord"). Split from menu.c.
 * Original stateHighScoreTable @0x41dfd0 — draws 2 columns of the fshi
 * table for current level via commandDispatch, header strings, face
 * polygon + difficulty bar + time. Input: Right/Left cycle level,
 * Up/Down adjust row, Enter/Esc -> menuUpdate. */
extern int g_nLevelCount;           /* @0x45a6f4 number of levels */
extern int g_nResultsLevel;         /* @0x45d46c selected level index */
extern int g_nRecordsRow;           /* @0x45d47c selected row (0..1) */
extern int g_nScoreTableRow;        /* @0x45d464 iterator 0..1 */
extern int g_nScoreTableTick;       /* @0x45d43c tick counter */
extern float g_flHighScoreAnimTime; /* @0x45a714 anim accumulator */
extern int g_hMenuTexLevel;       /* @0x45a6b8 menu\level00.tpg */
extern int g_hMenuTexChar;        /* @0x45a6c0 menu\char00.tpg */

/* stateHighScoreTable @0x41dfd0 */
int stateHighScoreTable(int nType, int nKey, int nKeyType);

/* Record-table query contract — backs the commandDispatch "get/request"
 * record queries issued by the results-screen HUD (hud.c @0x412993 "get
 * fshi/vahi<lvl>time<slot>", @0x412e72 "request fshi/vahiscore",
 * @0x412b26/@0x412b4c "get/set toplevel"). The original served these from
 * its live config tree; the rebuild serves the decoded config.mm table
 * (recordEnsureLoaded runs the one-time decode). lvl/slot are clamped to
 * the table (level 0..4, slot 0..4); toplevel clamps to 0..10. */
void recordEnsureLoaded(void);
int  recordGetTime(int isVahi, int lvl, int slot);
int  recordGetTopLevel(void);
void recordSetTopLevel(int lvl);
void recordSubmitScore(int isVahi, int time, int slot, int face, int lvl);

#endif /* RECORD_H */
