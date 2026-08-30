#ifndef PLAYER_PHYSICS_H
#define PLAYER_PHYSICS_H

/* player_physics.h — gameUpdate cluster + special-zone ids
 * (maniac.exe 0x426ee0 physics suite). */

struct PlayerRecord;

extern int g_nObjIdMvnc; /* @0x450df4 "mvnc" */
extern int g_nObjIdHurl; /* @0x450dec "hurl" */
extern int g_nObjIdTele; /* @0x450de4 "tele" */

/* Physics passes — all __cdecl PlayerRecord* */
void playerUpdateWalkPhysics(struct PlayerRecord *pRec);        /* @0x426fd0 */
void playerUpdateOnFoot(struct PlayerRecord *pRec);             /* @0x427730 */
void playerUpdateCartPhysics(struct PlayerRecord *pRec);        /* @0x4280b0 */
void syncWalkNodeChannelsToMesh(struct PlayerRecord *pRec);     /* @0x428990 */
void syncPosNodeChannelsToMesh(struct PlayerRecord *pRec);      /* @0x428840 */
void syncCartNodeChannelsToMeshes(struct PlayerRecord *pRec);   /* @0x428a70 */
void syncCartNodeChannelsToWalkPos(struct PlayerRecord *pRec);  /* @0x40e040 */
void gameUpdate(void);                                          /* @0x426ee0 */

#endif /* PLAYER_PHYSICS_H */
