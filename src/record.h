#ifndef RECORD_H
#define RECORD_H

#include <windows.h>

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
extern void *g_hMenuTexLevel;       /* @0x45a6b8 menu\level00.tpg */
extern void *g_hMenuTexChar;        /* @0x45a6c0 menu\char00.tpg */

/* stateHighScoreTable @0x41dfd0 */
int stateHighScoreTable(int nType, int nKey, int nKeyType);

#endif /* RECORD_H */
