#ifndef LEVEL0_H
#define LEVEL0_H

/* level0.h — level-0 (Riverside Mall) "MCDMAN bonus task" event director.
 *
 * Original functions:
 *   levelEventDirector_L0_Init    @0x416db0  round-start asset setup, dispatched
 *                                            by levelDirectorInits @0x40bdf0
 *   levelEventDirector_L0_Cleanup @0x416f70  frees the 7 event anims; dispatched
 *                                            by roundTeardown @0x40aa10 (jump
 *                                            table 0x40ad60, case 0 @0x40aace)
 *   levelEventDirector_L0         @0x416fd0  per-frame director from
 *                                            roundLogicUpdate @0x40c1c8
 *
 * The director bobs the SIGN_FELIXPO sign sub-meshes, and every ~10 s
 * (g_nObjUpdateTime idle ticks) runs the mascot flpick/throw presenter that
 * spawns a class-0x1f bonus pickup EventObject; the KASSOERSKA cashier plays
 * cash_sit.anm during play and s_winner.anm on the results screen. L1..L4
 * are implemented in level1.c..level4.c.
 */

void levelEventDirector_L0_Init(void);    /* @0x416db0 */
void levelEventDirector_L0_Cleanup(void); /* @0x416f70 */
void levelEventDirector_L0(void);         /* @0x416fd0 */

#endif /* LEVEL0_H */
