#ifndef STUBS_H
#define STUBS_H

#include "compat_types.h"

#include "gameplay.h"
#include "player.h"
#include "zone.h"

/* =====================================================================
 * Declared interfaces for unfinished behavior.
 * Each is a TODO stub: logs and performs a documented no-op or safe
 * placeholder transition so the vertical slice proceeds; callers keep their
 * contract unchanged when the stub body is later replaced. Original
 * target/address noted where one exists.
 * ===================================================================== */

/* Main-menu row targets (menu.c dispatch table g_kMenuRowTarget, entered
 * from menuUpdate @0x41b0b0 on Enter). Contract: state-func convention
 * (nType 0 = frame update, nType 1 + nKeyType 2 = keydown); each sets
 * g_pStateFunc to the next state. TODO stubs: log once + return to the
 * menu. Interfaces stay fixed when the real bodies replace them.
 * gotoOptions @0x41d300 is now implemented in options.c (Alternativ ->
 * Svårighetsgrad, Grafik ignored). */
int stateNetworkMenu(int nType, int nKey, int nKeyType);     /* @0x420190 */

/* Gameplay entry contracts used by stateLevelInit0..4. Player/world setup is
 * still deferred; commandDispatch routes the original run command to runCmd,
 * level startup scripts' "eload <file>.eo" to eloadCmd (src/obj_event.c),
 * and the record-table queries ("get fshi/vahi<lvl>time<slot>",
 * "get/set toplevel", "request fshi/vahiscore ...") to record.c. Anything
 * else returns NULL (callers must treat a NULL reply as "no data"). */
unsigned char *commandDispatch(intptr_t nCommand, LPCSTR pszCommand); /* @0x408b60 */

/* World-object sub-lists freed by objDtor @0x402ab0 (obj_world). The turret
 * object model is deferred; the player round-setup sub-objects never
 * populate these fields, so the stubs are safe no-ops. */
void objTurretListFree(int nMode);                           /* @0x402b40 */
void objTurretListFree2(int nMode);                          /* @0x402b70 */

/* Early-init deferred stubs — keep call hierarchy intact for
 * TrackRebuildDetailed. Real implementations will replace these when
 * the scene / string subsystems land. */

/* scenSetDir @0x432e60 and scenExpandNameList @0x432dd0 — implemented in
 * sen.c (sen.h declares them). */

/* netServerSendSubCmd @0x415d20 / netClientSendSubCmd @0x415cb0 — not
 * reconstructed and not called: networking is out of scope and the offline
 * branches no longer reference them. */

/* musicModuleInit @0x437b10 — init the music/streaming module: calls the
 * slot-allocator entry behind g_pMusicSlotAlloc @0x450f6c (original target
 * 0x431c30) with five callbacks (musicCbInitEmitter @0x437b40,
 * musicCbRet1 @0x437bf0, musicModulePosCheck @0x437b60, musicMixCb
 * @0x437c00) and sample rate 15000, returning the module handle which
 * roundStartInit stores in g_nMusicModuleHandle @0x4580bc. The module
 * entry chain and its callbacks are not reconstructed yet, so the stub
 * returns NULL and the rebuild stays music-silent. */
extern void *g_pMusicSlotAlloc;                              /* @0x450f6c */
void *musicModuleInit(void *pModuleEntry);                   /* @0x437b10 */

/* consoleHandleKey @0x4086e0 — not reconstructed and not called: the in-game
 * console is out of scope and g_nScrollText @0x4580ec is never set nonzero
 * here, so gameKeyHandler drops the branch. */

#endif /* STUBS_H */
