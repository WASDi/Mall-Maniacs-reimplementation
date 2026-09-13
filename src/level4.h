#ifndef LEVEL4_H
#define LEVEL4_H

/* level4.h — level-4 (Downtown Mall) "MCDMAN bonus task" event director.
 *
 * Original functions:
 *   levelEventDirector_L4_Init    @0x418f30  round-start asset setup, dispatched
 *                                            by levelDirectorInits @0x40bdf0
 *   levelEventDirector_L4_Cleanup @0x419240  frees the 7 event anims; dispatched
 *                                            by roundTeardown @0x40aa10 (jump
 *                                            table 0x40ad60, case 4)
 *   levelEventDirector_L4         @0x4192a0  per-frame director from
 *                                            roundLogicUpdate @0x40c1c8
 *
 * Same presenter family as L0..L3 (level0.c..level3.c): a 9-step mascot
 * flpick/throw state machine on a 10 s idle gate (g_nObjUpdateTime * tick
 * >= 10000) that spawns a class-0x1f bonus pickup EventObject consumed by
 * playerAiGrabItem @0x40ea20, and the KASSOERSKA cashier cash_sit/s_winner
 * ambient blink. Differences: no sign bobbing (unlike L0/L2) and no SKYLT
 * spin (unlike L3); the throw2 drop position is (54800,-1010,4200) with
 * mode-5 X landing steps (step*4-0xa0, as L2); Init registers five
 * positional sfx emitters (bank 1 idx 6/6/5/5/5) via sndPlaySfx3D
 * @0x42bcd0.
 */

void levelEventDirector_L4_Init(void);    /* @0x418f30 */
void levelEventDirector_L4_Cleanup(void); /* @0x419240 */
void levelEventDirector_L4(void);         /* @0x4192a0 */

#endif /* LEVEL4_H */
