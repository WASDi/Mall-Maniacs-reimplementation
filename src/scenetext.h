#ifndef SCENETEXT_H
#define SCENETEXT_H

#include <stddef.h>

/* Animated scene-text streams are TANI records from a .SEN file. Each glyph
 * is a 16-byte map-geometry record whose screen-space corners start at +8. */
extern int g_bSceneTextAnimActive;                 /* @0x45ecd0 */

void sceneTextAnimReset(void);                     /* @0x434a50 */
int  sceneTextAnimClose(void);                     /* @0x434bf0 */
int sceneTextAnimAdd(void *pOwner, void *pGlyphs, void *pRecordStream,
                     int nStreamLen);             /* @0x434a90 */
void sceneTextAnimUpdate(char nFrameStep);         /* @0x434b00 */

#endif /* SCENETEXT_H */