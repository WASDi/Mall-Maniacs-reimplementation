#ifndef OBJ_H
#define OBJ_H

/* obj.h — umbrella for the split obj.* subsystem (maniac.exe 0x402xxx/0x404xxx/0x414xxx).
 * The obj subsystem is split into three translation units:
 *   obj_event.c     — EventObject registry + .eo loader (hash buckets, point-in-zone)
 *   obj_world.c     — WorldNode list + turret/child-mesh + physics/fire passes
 *   obj_collision.c — ShotObj + shot-collision subsystem
 * This header preserves the single-include contract for existing consumers
 * (gameplay.c, player_physics.c, zone.h, player.h, …) — it re-exports all
 * three sub-headers. New code may include the sub-headers directly. */

#include "obj_event.h"
#include "obj_world.h"
#include "obj_collision.h"

#endif /* OBJ_H */
