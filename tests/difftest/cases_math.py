"""Leaf math / scene-channel cases (no globals, no heap)."""
import struct

from harness import ARENA_BASE, f32, fbits


def rnd_float(rng, lo=-1000.0, hi=1000.0):
    return f32(rng.uniform(lo, hi))


def arena_noise(rng, size=0x1000):
    # Untouched bytes are random so an unintended write always shows up.
    return bytearray(rng.getrandbits(8) for _ in range(size))


def gen_vec2_polar(rng):
    a = arena_noise(rng, 0x100)
    x, y = rnd_float(rng), rnd_float(rng)
    struct.pack_into("<ff", a, 0x10, x, y)
    return dict(args=[ARENA_BASE + 0x40, ARENA_BASE + 0x10], arena=a, ret="void",
                fields=[("out.x", 0x40, "f32"), ("out.y", 0x44, "f32")],
                inputs="in=(%r, %r)" % (x, y))


def gen_vec2_rotate_add(rng):
    a = arena_noise(rng, 0x100)
    ax, ay, bx, by = rnd_float(rng), rnd_float(rng, -7, 7), rnd_float(rng), rnd_float(rng, -7, 7)
    struct.pack_into("<ffff", a, 0x10, ax, ay, bx, by)
    return dict(args=[ARENA_BASE + 0x40, ARENA_BASE + 0x10, ARENA_BASE + 0x18], arena=a, ret="void",
                fields=[("out.x", 0x40, "f32"), ("out.y", 0x44, "f32")],
                inputs="A=(%r, %r) B=(%r, %r)" % (ax, ay, bx, by))


def gen_scene_node_face_pos(rng):
    # SceneNode at +0, its SceneChannel array (stride 0x70) at +0x100.
    a = arena_noise(rng, 0x800)
    count = rng.choice([1, 2, 3, 4, rng.randrange(256)])
    a[3] = count
    struct.pack_into("<I", a, 0x14, ARENA_BASE + 0x100)
    nch = rng.choice([0, 1, 2, 3, -1, count, rng.randrange(-4, 8)])
    for i in range(8):
        struct.pack_into("<iii", a, 0x100 + 0x70 * i + 0x10,
                         *(rng.randrange(-5000, 5000) for _ in range(3)))
    fx, fy, fz = rnd_float(rng, -6000, 6000), rnd_float(rng, -6000, 6000), rnd_float(rng, -6000, 6000)
    mode = rng.choice([2, 2, 2, 1, 0, 3])
    fields = []
    for i in range(8):
        base = 0x100 + 0x70 * i
        fields += [("ch[%d].rot[0]" % i, base, "i16"), ("ch[%d].rot[1]" % i, base + 2, "i16"),
                   ("ch[%d].rot[2]" % i, base + 4, "i16"), ("ch[%d].bFlagA" % i, base + 0xA, "u8"),
                   ("ch[%d].bFlagB" % i, base + 0xB, "u8")]
    return dict(args=[ARENA_BASE, nch & 0xFFFFFFFF, fbits(fx), fbits(fy), fbits(fz), mode],
                arena=a, ret="int", fields=fields,
                inputs="count=%d ch=%d pos=(%r, %r, %r) mode=%d" % (count, nch, fx, fy, fz, mode))


TESTS = {
    # name: (original address, original convention, rebuild symbol, generator)
    "mathVec2Polar":    (0x435060, "thiscall", "mathVec2Polar", gen_vec2_polar),
    "gxVec2RotateAdd":  (0x435090, "cdecl", "gxVec2RotateAdd", gen_vec2_rotate_add),
    "sceneNodeFacePos": (0x431030, "cdecl", "sceneNodeFacePos", gen_scene_node_face_pos),
}
