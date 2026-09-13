#ifndef LEVEL3_H
#define LEVEL3_H

/* level3.h — level-3 (Oriental Mall) "MCDMAN bonus task" event director.
 *
 * Original functions:
 *   levelEventDirector_L3_Init    @0x4186c0  round-start asset setup, dispatched
 *                                            by levelDirectorInits @0x40bdf0
 *   levelEventDirector_L3_Cleanup @0x4189d0  frees the 7 event anims; dispatched
 *                                            by roundTeardown @0x40aa10 (jump
 *                                            table 0x40ad60, case 3)
 *   levelEventDirector_L3         @0x418a30  per-frame director from
 *                                            roundLogicUpdate @0x40c1c8
 *
 * Same presenter family as L0/L1/L2 (level0.c/level1.c/level2.c): a
 * 9-step mascot flpick/throw state machine on a 10 s idle gate
 * (g_nObjUpdateTime * tick >= 10000) that spawns a class-0x1f bonus
 * pickup EventObject consumed by playerAiGrabItem @0x40ea20, and the
 * KASSOERSKA cashier cash_sit/s_winner ambient blink. Differences from
 * L0/L2: no SIGN_FELIXPO bobbing — instead seven SKYLT%d sign nodes
 * rotate in pitch ((i*5+0x28)*10, mode 5); the throw2 drop position is
 * (-39000,3010,-23200) with mode-5 X landing steps ((0x28-step)*4);
 * Init registers four positional sfx emitters (bank 1 idx
 * 0x1b/0x1b/0x19/0x1b) via sndPlaySfx3D @0x42bcd0.
 */

void levelEventDirector_L3_Init(void);    /* @0x4186c0 */
void levelEventDirector_L3_Cleanup(void); /* @0x4189d0 */
void levelEventDirector_L3(void);         /* @0x418a30 */

#endif /* LEVEL3_H */
