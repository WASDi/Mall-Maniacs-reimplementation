#ifndef LEVEL2_H
#define LEVEL2_H

/* level2.h — level-2 (Harbour Mall) "MCDMAN bonus task" event director.
 *
 * Original functions:
 *   levelEventDirector_L2_Init    @0x417dc0  round-start asset setup, dispatched
 *                                            by levelDirectorInits @0x40bdf0
 *   levelEventDirector_L2_Cleanup @0x417fa0  frees the 7 event anims; dispatched
 *                                            by roundTeardown @0x40aa10 (jump
 *                                            table 0x40ad60, case 2)
 *   levelEventDirector_L2         @0x418000  per-frame director from
 *                                            roundLogicUpdate @0x40c1c8
 *
 * Same presenter family as L0 (level0.c): SIGN_FELIXPO sign bobbing, a
 * 9-step mascot flpick/throw state machine on a 10 s idle gate
 * (g_nObjUpdateTime * tick >= 10000) that spawns a class-0x1f bonus
 * pickup EventObject consumed by playerAiGrabItem @0x40ea20, and the
 * KASSOERSKA cashier cash_sit/s_winner ambient blink. Differences from
 * L0: the KASSOERSKA/MCDMAN alloc-child positions, the throw2 drop
 * position (0x81b0,-1510,800) with mode-5 X-slide landing steps
 * (step*4-0xa0), and a trailing cash-zone proximity beep (sfx bank 0
 * idx 0x23) while the local player stands within 2500 of the "gong"
 * zone object @0x4500cc.
 */

void levelEventDirector_L2_Init(void);    /* @0x417dc0 */
void levelEventDirector_L2_Cleanup(void); /* @0x417fa0 */
void levelEventDirector_L2(void);         /* @0x418000 */

#endif /* LEVEL2_H */
