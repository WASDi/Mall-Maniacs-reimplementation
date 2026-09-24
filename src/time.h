#ifndef TIME_H
#define TIME_H

#include "compat_types.h"

/* Early timing / MCI helpers — mirrors original maniac addresses. */
int  getGameTime(void);                 /* @0x40dfe0 */
void winmmInitTimerRes(void);           /* @0x40dfd0 */
void winmmRestoreTimerRes(void);        /* @0x40e030 */
void nopDebugStub(void);                /* @0x401590 */
int  mciPlayCdaudio(int nTrack); /* @0x416cc0 (HWND residue dropped; silent stub) */
int  mciStopCdaudio(void);              /* @0x416c80 */

extern int g_nClock;
extern int g_nClockCache;
extern int g_nFrameDue;

#endif /* TIME_H */
