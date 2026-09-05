#ifndef LEVEL1_H
#define LEVEL1_H

/* level1.h — level-1 (Woodland Mall) "MCDMAN bonus task" event director.
 *
 * Original functions:
 *   levelEventDirector_L1_Init    @0x4175e0  round-start asset setup, dispatched
 *                                            by levelDirectorInits @0x40bdf0
 *   levelEventDirector_L1_Cleanup @0x417860  frees the 7 event anims; dispatched
 *                                            by roundTeardown @0x40aa10 (jump
 *                                            table 0x40ad60, case 1)
 *   levelEventDirector_L1         @0x4178c0  per-frame director from
 *                                            roundLogicUpdate @0x40c1c8
 *
 * Same 9-step presenter as L0 (level0.c) minus the SIGN_FELIXPO sign
 * bobbing: every ~10 s (g_nObjUpdateTime idle ticks) the mascot runs
 * flpick1/flpick2/throw1, attaches the hidden BURGER prop to its class
 * mesh slot 8, throws it, drops it onto the counter in four mode-5
 * z-steps and spawns a class-0x1f bonus pickup EventObject consumed by
 * playerAiGrabItem @0x40ea20 via EventObject.field_14. The KASSOERSKA
 * cashier plays cash_sit.anm during play and s_winner.anm on the results
 * screen. Init additionally registers three positional sfx emitters
 * (bank 1 idx 0x20/0x1f/0x1f) via sndPlaySfx3D @0x42bcd0.
 */

void levelEventDirector_L1_Init(void);    /* @0x4175e0 */
void levelEventDirector_L1_Cleanup(void); /* @0x417860 */
void levelEventDirector_L1(void);         /* @0x4178c0 */

#endif /* LEVEL1_H */
