"""World-object and sound-emitter cases: globals, heap and rand in play.

Layouts (32-bit, verified against the original's disassembly):
  WorldNode    0x48  +0 pPrev +4 pNext +8 pChildMeshHead +0xc pTurretHead
                     +0x10 pShotList +0x14..+0x1c ints +0x20 vPos +0x28 vPosB
                     +0x30 flScaleA +0x34 flScaleB +0x38 flScaleC +0x3c field_3c
                     +0x40 flImpulse +0x44 pParent
  ObjChildMesh 0x4c  +0 nMeshId +4/+8 nValue1/2 +0xc nChannelKey +0x10..+0x18 extents
                     +0x1c vPolar +0x24 vPos +0x2c vWorldA +0x34 vWorldB
                     +0x3c pSurface +0x40 flHeight +0x44 flVertVel +0x48 pNext
  ObjTurret    0x1c  +0 nTypeId +4 flAngle +8 vPolar +0x10 vPos +0x18 pNext
  SndEmitter   0x1c  +0 nSndId +4 pPrev(older) +8 pNext(newer) +0xc pPosNode
                     +0x10 pMusicEmitter +0x14 nSfxHandle +0x18 nFlags
  SceneNode    0xa8  +0 nId(u16) +2 bType +3 nChannelCount +4 pParent +8 pNextSib
                     +0xc pChild +0x10 pPrevLink +0x14 pChannels +0x18/+0x1c radii
                     +0x20 pTypeDef, channel at +0x38: +0 rot[3] +6 fUnk6 +0xa/+0xb
                     flags +0xc nIdx +0x10 x/y/z
"""
import struct

from cases_math import arena_noise, rnd_float
from harness import ARENA_BASE, fbits

NODE_SIZE, MESH_SIZE, TURRET_SIZE, EMIT_SIZE, SCENE_SIZE = 0x48, 0x4C, 0x1C, 0x1C, 0xA8

NODE_FIELDS = [("pPrev", 0x0, "u32"), ("pNext", 0x4, "u32"), ("pChildMeshHead", 0x8, "u32"),
               ("pTurretHead", 0xC, "u32"), ("pShotList", 0x10, "u32"), ("field_14", 0x14, "i32"),
               ("field_18", 0x18, "i32"), ("field_1c", 0x1C, "i32"), ("vPos.x", 0x20, "f32"),
               ("vPos.y", 0x24, "f32"), ("vPosB.x", 0x28, "f32"), ("vPosB.y", 0x2C, "f32"),
               ("flScaleA", 0x30, "f32"), ("flScaleB", 0x34, "f32"), ("flScaleC", 0x38, "f32"),
               ("field_3c", 0x3C, "f32"), ("flImpulse", 0x40, "f32"), ("pParent", 0x44, "u32")]
MESH_FIELDS = [("nMeshId", 0x0, "i32"), ("nValue1", 0x4, "i32"), ("nValue2", 0x8, "i32"),
               ("nChannelKey", 0xC, "u32"), ("flExtentB", 0x10, "f32"), ("flExtentA", 0x14, "f32"),
               ("flExtentC", 0x18, "f32"), ("vPolar.x", 0x1C, "f32"), ("vPolar.y", 0x20, "f32"),
               ("vPos.x", 0x24, "f32"), ("vPos.y", 0x28, "f32"), ("vWorldA.x", 0x2C, "f32"),
               ("vWorldA.y", 0x30, "f32"), ("vWorldB.x", 0x34, "f32"), ("vWorldB.y", 0x38, "f32"),
               ("pSurface", 0x3C, "u32"), ("flHeight", 0x40, "f32"), ("flVertVel", 0x44, "f32"),
               ("pNext", 0x48, "u32")]
TURRET_FIELDS = [("nTypeId", 0x0, "u32"), ("flAngle", 0x4, "f32"), ("vPolar.x", 0x8, "f32"),
                 ("vPolar.y", 0xC, "f32"), ("vPos.x", 0x10, "f32"), ("vPos.y", 0x14, "f32"),
                 ("pNext", 0x18, "u32")]
EMIT_FIELDS = [("nSndId", 0x0, "i32"), ("pPrev", 0x4, "u32"), ("pNext", 0x8, "u32"),
               ("pPosNode", 0xC, "u32"), ("pMusicEmitter", 0x10, "u32"), ("nSfxHandle", 0x14, "i32"),
               ("nFlags", 0x18, "u32")]
SCENE_FIELDS = [("nId", 0x0, "u16"), ("pParent", 0x4, "u32"), ("pNextSib", 0x8, "u32"),
                ("pChild", 0xC, "u32"), ("pPrevLink", 0x10, "u32"), ("pChannels", 0x14, "u32"),
                ("nBoundingRadiusA", 0x18, "i32"), ("nBoundingRadiusB", 0x1C, "i32"),
                ("ch.nIdx", 0x44, "i32"), ("ch.x", 0x48, "i32"), ("ch.y", 0x4C, "i32"),
                ("ch.z", 0x50, "i32")]

CHANNEL_KEYS = [0x1111, 0x2222, 0x3333]


def fields_at(prefix, base, table):
    return [("%s.%s" % (prefix, n), base + off, t) for n, off, t in table]


class World:
    """Builds WorldNode lists with child meshes/turrets inside an arena."""

    def __init__(self, rng, size=0x4000):
        self.rng = rng
        self.a = arena_noise(rng, size)
        self.cur = 0x100
        self.fields = []

    def alloc(self, size):
        off = self.cur
        self.cur += (size + 15) & ~15
        return off

    def node(self, n_meshes=None, n_turrets=None, mesh_id=None):
        rng, a = self.rng, self.a
        off = self.alloc(NODE_SIZE)
        struct.pack_into("<IIIII", a, off, 0, 0, 0, 0, 0)
        struct.pack_into("<iii", a, off + 0x14, rng.randrange(0, 3), rng.randrange(0, 3), rng.choice([0, 1]))
        struct.pack_into("<10f", a, off + 0x20, *(rnd_float(rng, -3000, 3000) for _ in range(4)),
                         *(rnd_float(rng, -3.2, 3.2) for _ in range(3)), rnd_float(rng, 0, 50),
                         rnd_float(rng), 0.0)
        struct.pack_into("<I", a, off + 0x44, 0)
        self.fields += fields_at("node@%x" % off, off, NODE_FIELDS)
        head = 0
        for _ in range(rng.randrange(0, 4) if n_meshes is None else n_meshes):
            m = self.alloc(MESH_SIZE)
            mid = rng.choice([0, 1]) if mesh_id is None else mesh_id
            struct.pack_into("<iiiI", a, m, mid, rng.randrange(-50, 50), rng.randrange(-50, 50),
                             rng.choice(CHANNEL_KEYS))
            struct.pack_into("<11f", a, m + 0x10, *(rnd_float(rng, 0, 300) for _ in range(3)),
                             rnd_float(rng, 0, 200), rnd_float(rng, -3.2, 3.2),
                             *(rnd_float(rng, -200, 200) for _ in range(6)))
            struct.pack_into("<Iff", a, m + 0x3C, 0, rnd_float(rng, -100, 400), rnd_float(rng, -30, 30))
            struct.pack_into("<I", a, m + 0x48, head)
            head = ARENA_BASE + m
            self.fields += fields_at("mesh@%x" % m, m, MESH_FIELDS)
        struct.pack_into("<I", a, off + 0x8, head)
        head = 0
        for _ in range(rng.randrange(0, 3) if n_turrets is None else n_turrets):
            t = self.alloc(TURRET_SIZE)
            struct.pack_into("<I5fI", a, t, rng.choice(CHANNEL_KEYS), rnd_float(rng, -3.2, 3.2),
                             *(rnd_float(rng, -200, 200) for _ in range(4)), head)
            head = ARENA_BASE + t
            self.fields += fields_at("turret@%x" % t, t, TURRET_FIELDS)
        struct.pack_into("<I", a, off + 0xC, head)
        return off

    def link(self, offs):
        """Chain nodes like objListPush does: g_pObjHead = last, pPrev toward older."""
        for older, newer in zip(offs, offs[1:]):
            struct.pack_into("<I", self.a, newer + 0x0, ARENA_BASE + older)
            struct.pack_into("<I", self.a, older + 0x4, ARENA_BASE + newer)
        return ARENA_BASE + offs[-1] if offs else 0

    def case(self, args, ret="void", globals_=None, inputs="", seed=None):
        return dict(args=args, arena=self.a[: self.cur + 0x40], ret=ret, fields=self.fields,
                    globals=globals_ or {}, inputs=inputs,
                    seed=self.rng.getrandbits(32) if seed is None else seed)


# ---------------------------------------------------------------- world nodes

def gen_obj_list_push(rng):
    w = World(rng)
    offs = [w.node(0, 0) for _ in range(rng.randrange(0, 4))]
    new = w.node(0, 0)
    head = w.link(offs)
    return w.case([ARENA_BASE + new], globals_={"g_pObjHead": head},
                  inputs="%d nodes already listed" % len(offs))


def gen_obj_dtor(rng):
    w = World(rng)
    offs = [w.node() for _ in range(rng.randrange(1, 5))]
    head = w.link(offs)
    victim = rng.choice(offs)
    return w.case([ARENA_BASE + victim], globals_={"g_pObjHead": head},
                  inputs="list of %d, destroy #%d" % (len(offs), offs.index(victim)))


def gen_obj_move_polar(rng):
    w = World(rng)
    n = w.node()
    ln, ang = rnd_float(rng, -500, 500), rnd_float(rng, -7, 7)
    return w.case([ARENA_BASE + n, fbits(ln), fbits(ang)], inputs="len=%r ang=%r" % (ln, ang))


def gen_obj_set_angle(rng):
    w = World(rng)
    n = w.node()
    ang = rnd_float(rng, -7, 7)
    return w.case([ARENA_BASE + n, fbits(ang)], inputs="angle=%r" % ang)


def gen_node_channel_avg(rng):
    w = World(rng)
    n = w.node(n_meshes=rng.randrange(0, 5))
    key = rng.choice(CHANNEL_KEYS + [0x4444])
    return w.case([ARENA_BASE + n, key], ret="float", inputs="key=0x%x" % key)


def gen_obj_find_turret(rng):
    w = World(rng)
    n = w.node(n_meshes=rng.randrange(0, 5))
    key = rng.choice(CHANNEL_KEYS + [0x4444])
    return w.case([ARENA_BASE + n, key], ret="int", inputs="key=0x%x" % key)


def gen_obj_turret_add(rng):
    w = World(rng)
    n = w.node()
    px, py, pz = (rng.randrange(-4000, 4000) for _ in range(3))
    ang = rng.randrange(-32768, 32768)
    typ = rng.choice(CHANNEL_KEYS)
    case = w.case([ARENA_BASE + n, px, py, pz, ang & 0xFFFFFFFF, typ],
                  inputs="pos=(%d,%d,%d) angle=%d" % (px, py, pz, ang))
    case["heap_fields"] = fields_at("newTurret", 0, TURRET_FIELDS)
    return case


def gen_node_set_transform(rng):
    w = World(rng)
    n = w.node()
    px, py, pz, unk = (rng.randrange(-4000, 4000) for _ in range(4))
    ang = rng.randrange(-32768, 32768)
    return w.case([ARENA_BASE + n, px, py, pz, unk, ang & 0xFFFFFFFF],
                  inputs="pos=(%d,%d,%d) unk=%d angle=%d" % (px, py, pz, unk, ang))


def gen_obj_update_all(rng):
    # No collision meshes (nMeshId 0): exercises the list walk, per-frame
    # clears and the empty physics/fire passes without a nav mesh.
    w = World(rng)
    offs = [w.node(mesh_id=0) for _ in range(rng.randrange(0, 5))]
    head = w.link(offs)
    return w.case([], globals_={"g_pObjHead": head}, inputs="%d nodes" % len(offs))


# ---------------------------------------------------------------- sound emitters

def scene_node(w, parent, x, y, z, n_id=0, radius=0):
    """A one-channel SceneNode (rot 0) in the arena. parent: arena offset or None = g_rootNode."""
    off = w.alloc(SCENE_SIZE)
    struct.pack_into("<HBBIIIII", w.a, off, n_id, 0, 1, 0, 0, 0, 0, ARENA_BASE + off + 0x38)
    struct.pack_into("<iiI", w.a, off + 0x18, radius, radius, 0)
    struct.pack_into("<hhhfBBiiii", w.a, off + 0x38, 0, 0, 0, 1.0, 0, 0, 0, x, y, z)
    if parent is None:
        w.ptrs.append((off + 0x4, "g_rootNode", 0))
    else:
        struct.pack_into("<I", w.a, off + 0x4, ARENA_BASE + parent)
    w.fields += fields_at("scene@%x" % off, off, SCENE_FIELDS)
    return off


def link_children(w, parent, kids, root_hdr):
    """Head-insert kids under parent like the allocators: parent.pChild = last kid."""
    prev = None
    for k in kids:
        if prev is None:
            if parent is None:
                w.ptrs.append((k + 0x10, "g_rootNode", 0))
            else:
                struct.pack_into("<I", w.a, k + 0x10, ARENA_BASE + parent)
        else:
            struct.pack_into("<I", w.a, k + 0x10, ARENA_BASE + prev)
            struct.pack_into("<I", w.a, prev + 0x8, ARENA_BASE + k)
        prev = k
    # kids were linked oldest-first: the first is the list head
    if kids:
        if parent is None:
            struct.pack_into("<I", root_hdr, 0xC, ARENA_BASE + kids[0])
        else:
            struct.pack_into("<I", w.a, parent + 0xC, ARENA_BASE + kids[0])


def gen_snd_play_sfx3d(rng):
    w = World(rng)
    w.ptrs = []
    root_hdr = bytearray(0x38)
    struct.pack_into("<B", root_hdr, 3, 1)
    # Optional pos node under the root (rot 0, so world = local translation).
    pos = None
    pos_xyz = (0, 0, 0)
    if rng.random() < 0.6:
        pos_xyz = tuple(rng.randrange(-300, 300) for _ in range(3))
        pos = scene_node(w, None, *pos_xyz, n_id=1, radius=rng.randrange(0, 50))
    snd_id = rng.choice([1, 2, 3])
    nx, ny, nz = (rng.choice([0, rng.randrange(-200, 200)]) for _ in range(3))
    # Older emitters already on the list (head = newest), each with its
    # music node under the pos node or the root; some sit within the
    # dedupe radius (100) of (nx, ny, nz).
    olds, kids = [], {None: [], pos: []}
    for _ in range(rng.randrange(0, 4)):
        e = w.alloc(EMIT_SIZE)
        parent = rng.choice([None, pos])
        base = pos_xyz if parent is not None else (0, 0, 0)
        if rng.random() < 0.5:
            want = (nx + rng.randrange(-60, 60), ny + rng.randrange(-60, 60), nz + rng.randrange(-60, 60))
        else:
            want = tuple(rng.randrange(-400, 400) for _ in range(3))
        m = scene_node(w, parent, *(c - b for c, b in zip(want, base)))
        kids[parent].append(m)
        struct.pack_into("<iIIIIiI", w.a, e, rng.choice([1, 2, 3, snd_id]), 0, 0,
                         ARENA_BASE + parent if parent is not None else 0, ARENA_BASE + m, 0,
                         rng.choice([0, 4, 0xC]))
        olds.append(e)
    link_children(w, None, ([pos] if pos is not None else []) + kids[None], root_hdr)
    if pos is not None:
        link_children(w, pos, kids[pos], root_hdr)
    for older, newer in zip(olds, olds[1:]):
        struct.pack_into("<I", w.a, newer + 0x4, ARENA_BASE + older)
        struct.pack_into("<I", w.a, older + 0x8, ARENA_BASE + newer)
    new = w.alloc(EMIT_SIZE)
    for off in olds + [new]:
        w.fields += fields_at("emit@%x" % off, off, EMIT_FIELDS)
    flags = rng.choice([0, 4, 0xC, 0x10, 0x20, 0x24, 0x2C, rng.randrange(0, 0x40)])
    head = ARENA_BASE + olds[-1] if olds else 0
    tail = ARENA_BASE + olds[0] if olds else 0
    n_nodes = len(olds) + (pos is not None)
    handle = rng.choice([0, 0x100])
    slots = bytearray(0x1C0)
    struct.pack_into("<II", slots, 0, 1, rng.randrange(0, 200))
    args = [ARENA_BASE + new, 1, rng.randrange(0, 8), 65000, snd_id,
            ARENA_BASE + pos if pos is not None else 0, 0, nx, ny, nz, flags]
    case = w.case(args, ret="int",
                  globals_={"g_pSndEmitterHead": head, "g_pSndEmitterTail": tail,
                            "g_rootNode": bytes(root_hdr), "g_nMusicModuleHandle": handle,
                            "g_aMusicSlots": bytes(slots), "g_nSceneNodeCount": n_nodes,
                            "g_nSceneNodeCountPeak": n_nodes, "g_nSceneNodeMemUsed": 0xA8 * n_nodes,
                            "g_nSceneNodeMemPeak": 0xA8 * n_nodes},
                  inputs="%d older, id=%d flags=0x%x pos=(%d,%d,%d) posnode=%s handle=0x%x"
                  % (len(olds), snd_id, flags, nx, ny, nz, pos is not None, handle))
    case["arena_ptrs"] = w.ptrs
    case["heap_fields"] = fields_at("music", 0, SCENE_FIELDS)
    return case


TESTS = {
    "objListPush":     (0x4055C0, "cdecl", "objListPush", gen_obj_list_push),
    "objDtor":         (0x402AB0, "thiscall", "objDtor", gen_obj_dtor),          # __fastcall(ECX)
    "objMovePolar":    (0x404F10, "thiscall", "objMovePolar", gen_obj_move_polar),
    "objSetAngle":     (0x404F70, "thiscall", "objSetAngle", gen_obj_set_angle),
    "nodeChannelAvgFloat": (0x4050C0, "thiscall", "nodeChannelAvgFloat", gen_node_channel_avg),
    "objFindTurret":   (0x405120, "thiscall", "objFindTurret", gen_obj_find_turret),
    "objTurretAdd":    (0x405280, "thiscall", "objTurretAdd", gen_obj_turret_add),
    "nodeSetTransformFromChannels": (0x404E00, "thiscall", "nodeSetTransformFromChannels",
                                     gen_node_set_transform),
    "objUpdateAll":    (0x4055F0, "cdecl", "objUpdateAll", gen_obj_update_all),
    "sndPlaySfx3D":    (0x42BCD0, "thiscall", "sndPlaySfx3D", gen_snd_play_sfx3d),
}
